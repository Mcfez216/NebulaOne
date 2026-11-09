// NebulaOne - World::execute: the single entry point for every change a
// corporation (player or, later, a remote client) asks for.
#include <algorithm>

#include "world.h"

namespace nb {

Result corporateCommand(World& w, const Command& c);

static bool validStation(const World& w, Id s) { return s >= 0 && s < (Id)w.stations.size() && !w.stations[s].dead; }
static bool validFleet(const World& w, Id f, Id corp) {
    return f >= 0 && f < (Id)w.fleets.size() && w.fleets[f].alive && w.fleets[f].corp == corp;
}
static bool validShip(const World& w, Id s, Id corp) {
    return s >= 0 && s < (Id)w.ships.size() && w.ships[s].alive && w.ships[s].corp == corp;
}

// Fitting is done at a station: the ship in the hangar, or its fleet docked.
Result World::fit(Id corp, Id shipId, int slot, int moduleItem) {
    if (!validShip(*this, shipId, corp)) return Result::fail("Unknown ship.");
    Ship& s = ships[shipId];
    Id station = s.fleet == NONE ? s.station : fleets[s.fleet].docked;
    if (station == NONE) return Result::fail("Ships can only be refitted while docked.");
    const ShipDef& d = shipDef(s.def);
    if (slot < 0 || slot >= d.slots) return Result::fail("A " + d.name + " has " + std::to_string(d.slots) + " slots.");
    std::string nm = s.name.empty() ? d.name : s.name;
    if (moduleItem < 0) {
        if (s.fit[slot] < 0) return Result::fail("That slot is empty.");
        addItem(corp, station, MODULE_FIRST + s.fit[slot], 1);
        std::string was = item(MODULE_FIRST + s.fit[slot]).name;
        s.fit[slot] = -1;
        s.hull = std::min(s.hull, stats(s).hull);
        return Result::good(was + " removed from " + nm + " and put in the hangar.");
    }
    if (!isModule(moduleItem)) return Result::fail("That is not a ship module.");
    if (have(corp, station, moduleItem) <= 0) return Result::fail("No " + item(moduleItem).name + " in the hangar at " + stations[station].name + ".");
    takeItem(corp, station, moduleItem, 1);
    if (s.fit[slot] >= 0) addItem(corp, station, MODULE_FIRST + s.fit[slot], 1);
    s.fit[slot] = (int8_t)item(moduleItem).module;
    s.hull = stats(s).hull;  // the yard tops up the hull while it works
    return Result::good(item(moduleItem).name + " fitted to " + nm + ".");
}

Result World::execute(const Command& c) {
    if (c.corp < 0 || c.corp >= (Id)corps.size() || !corps[c.corp].alive) return Result::fail("Unknown corporation.");
    Corp& me = corps[c.corp];
    switch (c.type) {
        case Cmd::MarketBuy: {
            if (!validStation(*this, c.a) || c.item < 0 || c.item >= ITEM_COUNT) return Result::fail("Pick a station and item.");
            if (c.qty <= 0) return Result::fail("Enter a quantity.");
            if (!hasSight(c.corp, stations[c.a].system))
                return Result::fail("No live market feed at " + stations[c.a].name + ": get a fleet there, or place a limit order instead.");
            if (embargoed(c.a, c.item)) return Result::fail("The Charter has embargoed " + item(c.item).name + " in Charter space.");
            Money spent = 0;
            Money cap = c.price > 0 ? c.price : INT64_MAX / 4;
            int64_t got = instantBuy(c.corp, c.a, c.item, c.qty, cap, &spent);
            if (got <= 0) return Result::fail("Nothing for sale here at that price (or not enough credits).");
            return Result::good("Bought " + fmtInt(got) + " " + item(c.item).name + " for " + fmtMoney(spent) + ".", got, spent);
        }
        case Cmd::MarketSell: {
            if (!validStation(*this, c.a) || c.item < 0 || c.item >= ITEM_COUNT) return Result::fail("Pick a station and item.");
            if (c.qty <= 0) return Result::fail("Enter a quantity.");
            if (!hasSight(c.corp, stations[c.a].system))
                return Result::fail("No live market feed at " + stations[c.a].name + ": get a fleet there, or place a limit order instead.");
            if (embargoed(c.a, c.item)) return Result::fail("The Charter has embargoed " + item(c.item).name + " in Charter space.");
            Money got = 0;
            int64_t sold = instantSell(c.corp, c.a, c.item, c.qty, std::max<Money>(1, c.price), &got);
            if (sold <= 0) return Result::fail("No buyer here at that price.");
            Money tax = (Money)(got * salesTax(c.corp, c.a));
            return Result::good("Sold " + fmtInt(sold) + " " + item(c.item).name + " for " + fmtMoney(got - tax) + " after tax.", sold, got);
        }
        case Cmd::PlaceOrder:
            if (!validStation(*this, c.a)) return Result::fail("Pick a station.");
            return placeOrder(c.corp, c.a, c.item, c.flag, c.price, c.qty);
        case Cmd::CancelOrder:
            return cancelOrder(c.qty, c.corp) ? Result::good("Order cancelled; escrow returned.") : Result::fail("Order not found.");

        case Cmd::CreateFleet: {
            if (c.list.empty()) return Result::fail("Select at least one ship.");
            Id st = NONE;
            for (Id s : c.list) {
                if (!validShip(*this, s, c.corp) || ships[s].fleet != NONE) return Result::fail("A selected ship is not available.");
                if (st == NONE) st = ships[s].station;
                if (ships[s].station != st) return Result::fail("Ships must be docked at the same station to form a fleet.");
            }
            std::string name = c.text.empty() ? "Fleet " + std::to_string(me.fleetList.size() + 1) : c.text;
            Id id = createFleet(c.corp, c.list, name);
            if (id == NONE) return Result::fail("Could not form the fleet.");
            if (me.player)
                for (Id s : fleets[id].ships) {
                    if (ships[s].pilot != NONE) continue;
                    for (auto& p : people)
                        if (p.alive && p.employer == c.corp && p.role == Role::Pilot && p.ship == NONE && p.injuredUntil <= now) {
                            p.ship = s;
                            ships[s].pilot = p.id;
                            break;
                        }
                }
            Result r = Result::good("Formed " + name + ".");
            r.id = id;
            return r;
        }
        case Cmd::AddShip: {
            if (!validFleet(*this, c.a, c.corp)) return Result::fail("Unknown fleet.");
            Fleet& f = fleets[c.a];
            if (!validShip(*this, c.b, c.corp)) return Result::fail("Unknown ship.");
            Ship& s = ships[c.b];
            if (s.fleet != NONE) return Result::fail("That ship is not available.");
            if (f.docked == NONE || s.station != f.docked) return Result::fail("The fleet must be docked where the ship is.");
            s.fleet = f.id;
            f.ships.push_back(s.id);
            return Result::good("Ship added.");
        }
        case Cmd::RemoveShip: {
            if (!validFleet(*this, c.a, c.corp)) return Result::fail("Unknown fleet.");
            Fleet& f = fleets[c.a];
            if (f.docked == NONE) return Result::fail("Ships can only leave a fleet while docked.");
            auto it = std::find(f.ships.begin(), f.ships.end(), c.b);
            if (it == f.ships.end()) return Result::fail("That ship is not in this fleet.");
            if (f.ships.size() == 1) return Result::fail("Disband the fleet instead.");
            ships[c.b].fleet = NONE;
            ships[c.b].station = f.docked;
            f.ships.erase(it);
            return Result::good("Ship left the fleet and is in the hangar.");
        }
        case Cmd::DisbandFleet: {
            if (!validFleet(*this, c.a, c.corp)) return Result::fail("Unknown fleet.");
            Fleet& f = fleets[c.a];
            if (f.docked == NONE) return Result::fail("Dock the fleet first (give it a Move order).");
            unloadAll(f, f.docked);
            for (Id s : f.ships) { ships[s].fleet = NONE; ships[s].station = f.docked; }
            f.ships.clear();
            f.alive = false;
            return Result::good("Fleet disbanded; ships and cargo are in the hangar.");
        }
        case Cmd::SetFleetOrder: {
            if (!validFleet(*this, c.a, c.corp)) return Result::fail("Unknown fleet.");
            Fleet& f = fleets[c.a];
            const FleetOrder& o = c.order;
            switch (o.kind) {
                case OrderKind::Move:
                    if (!validStation(*this, o.station)) return Result::fail("Pick a destination station.");
                    break;
                case OrderKind::Mine:
                    if (o.system < 0 || o.system >= (Id)systems.size() || o.belt < 0 || o.belt >= (int)systems[o.system].belts.size() ||
                        !beltVisible(systems[o.system].belts[o.belt], c.corp))
                        return Result::fail("Pick a belt.");
                    if (!validStation(*this, o.station)) return Result::fail("Pick a station to unload at.");
                    break;
                case OrderKind::Haul:
                    if (!validStation(*this, o.station) || !validStation(*this, o.station2)) return Result::fail("Pick both stations.");
                    if (o.station == o.station2) return Result::fail("Pick two different stations.");
                    break;
                case OrderKind::Trade:
                    if (!validStation(*this, o.station) || !validStation(*this, o.station2)) return Result::fail("Pick both stations.");
                    if (o.item < 0 || isShipItem(o.item) || isBpo(o.item)) return Result::fail("Pick a tradable item.");
                    if (o.price <= 0) return Result::fail("Set the most you will pay per unit.");
                    break;
                case OrderKind::Patrol:
                case OrderKind::Raid:
                case OrderKind::Survey:
                case OrderKind::Scout:
                    if (o.system < 0 || o.system >= (Id)systems.size()) return Result::fail("Pick a system.");
                    break;
                case OrderKind::Salvage: {
                    const Site* site = nullptr;
                    for (auto& x : sites)
                        if (x.id == o.site) site = &x;
                    if (!site || site->finder != c.corp) return Result::fail("Pick one of your discovered sites.");
                    if (site->done || site->expires <= now) return Result::fail("That site is gone.");
                    if (site->kind != SiteKind::Derelict && site->kind != SiteKind::Relic) return Result::fail("Only derelicts and relic caches can be salvaged.");
                    if (o.station != NONE && !validStation(*this, o.station)) return Result::fail("Pick a station to bring the salvage to.");
                    break;
                }
                case OrderKind::Siege:
                    if (!validStation(*this, o.station) || !stations[o.station].isOutpost()) return Result::fail("Pick an outpost to besiege.");
                    if (stations[o.station].owner == c.corp) return Result::fail("That outpost is yours.");
                    break;
                case OrderKind::None: break;
            }
            if (f.battle != NONE) return Result::fail("The fleet is in combat.");
            FleetOrder o2 = o;
            if (o2.kind == OrderKind::Siege) o2.system = stations[o2.station].system;
            setOrder(f, o2);
            if (f.division >= 0 && me.player) {  // a direct order takes the fleet back from its director
                std::string d = divisionName(f.division);
                f.division = -1;
                return Result::good(std::string(orderKindName(o2.kind)) + " order set; " + f.name + " leaves the " + d + " division and takes orders from you.");
            }
            if (o2.kind == OrderKind::Raid && me.player && systems[o2.system].zone == Zone::Charter)
                return Result::good("Raid order set. Warning: attacks in Charter space bring the Charter Wardens down on you, and make you an outlaw.");
            if (o2.kind == OrderKind::Siege)
                return Result::good("Siege order set. " + corps[stations[o2.station].owner].name + " will treat you as an enemy from the first shot.");
            if (o2.kind == OrderKind::Raid && o2.targetCorp != NONE && atWar(c.corp, o2.targetCorp))
                return Result::good("Raid order set against " + corps[o2.targetCorp].name + ". You hold a war licence: attacking them is lawful everywhere.");
            return Result::good(std::string(orderKindName(o2.kind)) + " order set for " + f.name + ".");
        }
        case Cmd::RenameFleet:
            if (!validFleet(*this, c.a, c.corp)) return Result::fail("Unknown fleet.");
            if (c.text.empty()) return Result::fail("Enter a name.");
            fleets[c.a].name = c.text;
            return Result::good("Renamed.");

        case Cmd::Refine:
            if (!validStation(*this, c.a)) return Result::fail("Pick a station.");
            return startRefine(c.corp, c.a, c.item, c.qty);
        case Cmd::Manufacture:
            if (!validStation(*this, c.a)) return Result::fail("Pick a station.");
            return startManufacture(c.corp, c.a, c.item, c.qty);
        case Cmd::Research:
            if (!validStation(*this, c.a)) return Result::fail("Pick a station.");
            return startResearch(c.corp, c.a, c.item, c.flag ? JobKind::ResearchTE : JobKind::ResearchME, c.qty);
        case Cmd::CopyBlueprint:
            if (!validStation(*this, c.a)) return Result::fail("Pick a station.");
            return startResearch(c.corp, c.a, c.item, JobKind::Copy, c.qty);

        case Cmd::BankLoan: return takeBankLoan(c.corp, c.price, (int)c.qty);
        case Cmd::RepayLoan: {
            if (c.a < 0 || c.a >= (Id)loans.size()) return Result::fail("Unknown loan.");
            Loan& l = loans[c.a];
            if (l.borrower != c.corp || l.status != 0) return Result::fail("That loan is not yours to repay.");
            if (me.wallet < l.remaining) return Result::fail("You need " + fmtMoney(l.remaining) + " to repay it in full.");
            double interestShare = 1.0 - 1.0 / (1.0 + l.rate30 * l.days / 30.0);
            Money waive = (Money)(l.remaining * interestShare / 3);
            Money due = l.remaining - waive;
            pay(c.corp, -due, Cat::LoanRepay, "Early loan repayment");
            if (l.lender != NONE) pay(l.lender, due, Cat::LendRepaid, "Early repayment from " + me.name);
            l.remaining = 0;
            l.status = 1;
            return Result::good("Loan repaid early for " + fmtMoney(due) + ".");
        }
        case Cmd::FundRequest: return fundRequest(c.corp, c.a);
        case Cmd::BuyShares: return tradeShares(c.corp, c.a, c.qty, true);
        case Cmd::SellShares: return tradeShares(c.corp, c.a, c.qty, false);

        case Cmd::Hire: {
            if (c.a < 0 || c.a >= (Id)people.size()) return Result::fail("Unknown candidate.");
            Person& p = people[c.a];
            if (!p.alive || p.employer != NONE || p.hub == NONE) return Result::fail("That candidate is no longer available.");
            Money fee = p.wage * 3;
            if (me.wallet < fee) return Result::fail("Hiring costs 3 days' wages (" + fmtMoney(fee) + ").");
            pay(c.corp, -fee, Cat::Hiring, "Hired " + p.name);
            p.employer = c.corp;
            p.hub = NONE;
            p.hired = now;
            addLog(Chan::Corp, Sev::Good, me.player, NONE, p.name + " joins " + me.name + " as " + (p.role == Role::Pilot ? "a pilot." : "an executive."));
            return Result::good(p.name + " hired.");
        }
        case Cmd::Fire: {
            if (c.a < 0 || c.a >= (Id)people.size()) return Result::fail("Unknown person.");
            Person& p = people[c.a];
            if (p.employer != c.corp) return Result::fail("Not on your staff.");
            if (p.ship != NONE) { ships[p.ship].pilot = NONE; p.ship = NONE; }
            for (auto& e : me.exec)
                if (e == p.id) e = NONE;
            p.employer = NONE;
            p.alive = false;
            return Result::good(p.name + " has left the company.");
        }
        case Cmd::AssignPilot: {
            if (!validShip(*this, c.b, c.corp)) return Result::fail("Unknown ship.");
            Ship& s = ships[c.b];
            if (s.pilot != NONE) { people[s.pilot].ship = NONE; s.pilot = NONE; }
            if (c.a == NONE) return Result::good("Pilot removed.");
            if (c.a < 0 || c.a >= (Id)people.size()) return Result::fail("Unknown person.");
            Person& p = people[c.a];
            if (p.employer != c.corp || p.role != Role::Pilot || !p.alive) return Result::fail("Not one of your pilots.");
            if (p.ship != NONE) ships[p.ship].pilot = NONE;
            p.ship = s.id;
            s.pilot = p.id;
            return Result::good(p.name + " now flies " + (s.name.empty() ? "this " + shipDef(s.def).name : s.name) + ".");
        }
        case Cmd::AssignExec: {
            if (c.b < 0 || c.b >= SEAT_COUNT) return Result::fail("Unknown seat.");
            if (c.a == NONE) { me.exec[c.b] = NONE; return Result::good("Seat vacated."); }
            if (c.a < 0 || c.a >= (Id)people.size()) return Result::fail("Unknown person.");
            Person& p = people[c.a];
            if (p.employer != c.corp || p.role != Role::Executive) return Result::fail("Not one of your executives.");
            for (auto& e : me.exec)
                if (e == p.id) e = NONE;
            me.exec[c.b] = p.id;
            return Result::good(p.name + " is now " + seatName(c.b) + ".");
        }
        case Cmd::SetInsurance: {
            if (!validShip(*this, c.a, c.corp)) return Result::fail("Unknown ship.");
            Ship& s = ships[c.a];
            if (!c.flag) { s.insured = false; return Result::good("Hull insurance will not renew."); }
            if (s.insured && s.insuredUntil > now) return Result::fail("Already insured.");
            Money prem = insurancePremium(s);
            if (me.wallet < prem) return Result::fail("Premium is " + fmtMoney(prem) + ".");
            pay(c.corp, -prem, Cat::Insurance, "Hull insurance: " + (s.name.empty() ? shipDef(s.def).name : s.name));
            s.insured = true;
            s.insuredUntil = now + 30 * MINUTES_PER_DAY;
            return Result::good("Insured for 30 days for " + fmtMoney(prem) + "; pays 70% of hull and fittings on loss.");
        }

        case Cmd::FitModule: return fit(c.corp, c.a, (int)c.b, c.item);
        case Cmd::UnfitModule: return fit(c.corp, c.a, (int)c.b, -1);
        case Cmd::RenameShip:
            if (!validShip(*this, c.a, c.corp)) return Result::fail("Unknown ship.");
            if (c.text.empty() || c.text.size() > 40) return Result::fail("Enter a name of up to 40 characters.");
            ships[c.a].name = c.text;
            return Result::good("Renamed.");

        case Cmd::AcceptContract: return acceptContract(c.corp, c.a);
        case Cmd::DeliverContract: return deliverContract(c.corp, c.a);
        case Cmd::PostContract: return postContract(c.corp, c.contract);
        case Cmd::CancelContract: {
            if (c.a < 0 || c.a >= (Id)contracts.size()) return Result::fail("Unknown contract.");
            Contract& k = contracts[c.a];
            if (k.issuer != c.corp) return Result::fail("Only the issuer can cancel a contract.");
            if (k.state != ContractState::Open) return Result::fail("It has already been taken.");
            pay(c.corp, k.reward, Cat::Contracts, "Contract cancelled: reward returned");
            if (k.kind == ContractKind::Courier) addItem(c.corp, k.from, k.item, k.qty);
            k.state = ContractState::Cancelled;
            return Result::good("Contract withdrawn; escrow returned.");
        }

        case Cmd::AnchorOutpost: return anchorOutpost(c.corp, c.a);
        case Cmd::SetOutpostTax: {
            if (!validStation(*this, c.a) || !stations[c.a].isOutpost() || stations[c.a].owner != c.corp) return Result::fail("Not your outpost.");
            if (c.f < 0 || c.f > 0.25f || c.f2 < 0 || c.f2 > 0.25f) return Result::fail("Taxes run from 0% to 25%.");
            stations[c.a].marketTax = c.f;
            stations[c.a].refineTax = c.f2;
            return Result::good("Outpost taxes set.");
        }

        case Cmd::GoPublic:
        case Cmd::DeclareDividend:
        case Cmd::Buyback:
        case Cmd::Merge: return corporateCommand(*this, c);

        case Cmd::SetLivery:
            me.livery = (uint8_t)std::clamp((int)c.qty, 0, 7);
            return Result::good("Livery changed.");
        case Cmd::BuyIntel: return buyIntel(c.corp, c.a, c.flag);
        case Cmd::DeclareWar: return declareWar(c.corp, c.a);
        case Cmd::ProposePact: return proposePact(c.corp, c.a, c.flag ? Pact::Alliance : Pact::NonAggression);
        case Cmd::ProposePeace: return proposePeace(c.corp, c.a, c.price);
        case Cmd::RespondOffer: return respondOffer(c.corp, c.a, c.flag);
        case Cmd::CancelPact: return cancelPact(c.corp, c.a);
        case Cmd::HireMercs: return hireMercs(c.corp, c.a, c.b, (int)c.qty);
        case Cmd::SetDivision: return setDivision(c.corp, (int)c.a, c.policy);
        case Cmd::AssignDivision: return assignDivision(c.corp, c.a, (int)c.b);
        case Cmd::AnswerStaff: return answerStaff(c.corp, c.a, c.flag);
        case Cmd::StartSpyOp: return startSpyOp(c.corp, c.b, (SpyKind)std::clamp((int)c.a, 0, SPY_KIND_COUNT - 1), c.item);
        case Cmd::FoundProject:
            return foundProject(c.corp, (ProjectKind)std::clamp((int)c.a, 0, PROJECT_KIND_COUNT - 1), c.b, (Id)c.qty, (Id)c.item);
        case Cmd::UpgradeStation: return upgradeStation(c.corp, c.a, (int)c.b);
        case Cmd::ContributeProject: return contributeProject(c.corp, c.a, c.price, c.flag);
        case Cmd::SettleColony: return settleColony(c.corp, c.a, (int)c.b);
        case Cmd::UpgradeColony: return upgradeColony(c.corp, c.a, (int)c.b);
        case Cmd::SupplyColony: return supplyColony(c.corp, c.a);
        // capital markets: a = hub/target/bond/position, b = days, item, qty, flag = long
        case Cmd::OpenFuture: return openFuture(c.corp, c.a, c.item, c.qty, c.flag, (int)c.b);
        case Cmd::CloseFuture: return closeFuture(c.corp, c.a);
        case Cmd::IssueBond: return issueBond(c.corp, c.qty, (int)c.b);
        case Cmd::BuyBond: return buyBond(c.corp, c.a, c.qty);
        case Cmd::SellBond: return sellBond(c.corp, c.a, c.qty);
        case Cmd::OpenShort: return openShort(c.corp, c.a, c.qty);
        case Cmd::CloseShort: return closeShort(c.corp, c.a);
        case Cmd::AdoptPill: return adoptPill(c.corp);
        case Cmd::WhiteKnight: return whiteKnight(c.corp, c.a);
        case Cmd::PayGreenmail: return payGreenmail(c.corp);
        case Cmd::SetUnderwriting: return setUnderwriting(c.corp, c.uw);
    }
    return Result::fail("Unknown command.");
}

}  // namespace nb

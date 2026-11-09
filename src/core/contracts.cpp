// NebulaOne - the contract board: supply, courier, bounty and patrol jobs.
//
// Stations post supply and courier work, the Charter Authority posts bounties
// on outlaw corporations, and miners pay for patrols in risky sectors.
// Corporations (the player included) can post their own.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

static Id addContract(World& w, Contract c) {
    c.id = (Id)w.contracts.size();
    c.posted = w.now;
    w.contracts.push_back(c);
    return c.id;
}

static Money roundTo(Money v, Money step) { return std::max(step, (v + step / 2) / step * step); }

void World::contractsDaily() {
    // close out the old
    for (auto& c : contracts) {
        if (c.state == ContractState::Open && c.expires <= now) {
            c.state = ContractState::Expired;
            if (c.issuer != NONE) {  // refund what the issuer put up
                pay(c.issuer, c.reward, Cat::Contracts, "Contract expired: reward returned");
                if (c.kind == ContractKind::Courier && c.item >= 0) addItem(c.issuer, c.from, c.item, c.qty);
            }
        } else if (c.state == ContractState::Accepted && c.deadline <= now) {
            c.state = ContractState::Failed;
            if (c.collateral > 0 && c.issuer != NONE) pay(c.issuer, c.collateral, Cat::Contracts, "Collateral from a failed courier contract");
            if (c.issuer != NONE) pay(c.issuer, c.reward, Cat::Contracts, "Contract failed: reward returned");
            if (c.contractor == player)
                addLog(Chan::Corp, Sev::Bad, true, NONE, std::string(contractKindName(c.kind)) + " contract failed: " + c.issuerName +
                                                             (c.collateral > 0 ? ". Your collateral of " + fmtMoney(c.collateral) + " is forfeit." : "."));
            if (c.contractor != NONE && c.issuer != NONE) addStanding(c.issuer, c.contractor, -2.0f);
        }
    }
    if (contracts.size() > 600) {
        contracts.erase(std::remove_if(contracts.begin(), contracts.end(),
                                       [&](const Contract& c) {
                                           return c.state != ContractState::Open && c.state != ContractState::Accepted && now - c.posted > 20 * MINUTES_PER_DAY;
                                       }),
                        contracts.end());
        for (size_t i = 0; i < contracts.size(); i++) contracts[i].id = (Id)i;
        for (auto& f : fleets)
            if (f.order.contract != NONE) f.order.contract = NONE;  // ids moved; the delivery is still possible by hand
    }
    int open = 0;
    for (auto& c : contracts) open += c.state == ContractState::Open;
    if (open >= 120) return;

    // ---- new work, mostly near the player so the board stays relevant on the largest charts
    Id homeSys = stations[pc().home].system;
    std::vector<Id> near = systemsWithin(homeSys, 10);
    std::vector<Id> nearStations;
    for (Id s : near)
        for (Id st : systems[s].stations)
            if (stationUsable(st) && !stations[st].isOutpost()) nearStations.push_back(st);
    if (nearStations.empty()) return;
    int want = 4 + (int)std::min<size_t>(8, stations.size() / 200);
    for (int k = 0; k < want; k++) {
        double r = rng.uniform();
        Id st = nearStations[rng.irange(0, (int)nearStations.size() - 1)];
        Station& S = stations[st];
        if (r < 0.50) {
            // supply: a station short of something
            int best = -1;
            double worst = 0.7;
            for (int t = 0; t < 12; t++) {
                int i = rng.chance(0.5) ? GOOD_FIRST + rng.irange(0, GOOD_COUNT - 1)
                                        : rng.chance(0.5) ? MINERAL_FIRST + rng.irange(0, 4) : COMPONENT_FIRST + rng.irange(0, COMPONENT_COUNT - 1);
                if (S.target[i] <= 0) continue;
                double f = S.stock[i] / S.target[i];
                if (f < worst) { worst = f; best = i; }
            }
            if (best < 0) continue;
            Contract c;
            c.kind = ContractKind::Supply;
            c.issuerName = S.name;
            c.to = st;
            c.item = best;
            double vol = rng.range(800, 5000);
            c.qty = std::max<int64_t>(10, (int64_t)(vol / item(best).volume));
            c.qty = std::min<int64_t>(c.qty, (int64_t)std::max(10.0, (double)S.target[best]));
            c.reward = roundTo((Money)(refPrice(st, best) * c.qty * rng.range(1.22, 1.38)), 1000 * CR);
            c.expires = now + 3 * MINUTES_PER_DAY;
            c.goal = 6;  // days allowed once accepted
            addContract(*this, c);
        } else if (r < 0.78) {
            // courier: consignment goods from one station to another
            std::vector<Id> dests;
            std::vector<int> dd;
            std::vector<Id> around = systemsWithin(S.system, 9, &dd);
            for (size_t k = 0; k < around.size(); k++)
                if (dd[k] >= 3)
                    for (Id d : systems[around[k]].stations)
                        if (stationUsable(d) && !stations[d].isOutpost()) dests.push_back(d);
            if (dests.empty()) continue;
            Id to = dests[rng.irange(0, (int)dests.size() - 1)];
            int i = GOOD_FIRST + rng.irange(0, GOOD_COUNT - 1);
            Contract c;
            c.kind = ContractKind::Courier;
            c.issuerName = S.name + " consignment";
            c.from = st;
            c.to = to;
            c.item = i;
            c.qty = std::max<int64_t>(10, (int64_t)(rng.range(2000, 9000) / item(i).volume));
            Money value = item(i).base * c.qty;
            auto path = route(S.system, stations[to].system, RoutePolicy::Shortest);
            double risk = routeRisk(path, S.system);
            c.collateral = roundTo(value, 10000 * CR);
            c.reward = roundTo((Money)(std::max<double>(150000.0 * CR, value * 0.03) * (1.0 + 0.15 * path.size()) * (1.0 + risk / 50.0)), 1000 * CR);
            c.expires = now + 3 * MINUTES_PER_DAY;
            c.goal = 5;
            addContract(*this, c);
        } else if (r < 0.88) {
            // bounty from the Charter Authority on an outlaw corporation working nearby
            std::vector<Id> outlaws;
            for (auto& oc : corps)
                if (oc.alive && !oc.player && isOutlaw(oc.id) && jumps(homeSys, stations[oc.home].system) <= 14) outlaws.push_back(oc.id);
            if (outlaws.empty()) continue;
            Id t = outlaws[rng.irange(0, (int)outlaws.size() - 1)];
            bool dup = false;
            for (auto& c : contracts) dup = dup || (c.state == ContractState::Open && c.kind == ContractKind::Bounty && c.target == t);
            if (dup) continue;
            Contract c;
            c.kind = ContractKind::Bounty;
            c.issuerName = "Charter Authority";
            c.target = t;
            c.system = stations[corps[t].home].system;
            c.goal = rng.irange(2, 5);
            c.reward = roundTo((Money)(c.goal * rng.range(1.2e6, 1.8e6) * CR), 10000 * CR);
            c.expires = now + 4 * MINUTES_PER_DAY;
            c.qty = 10;  // days allowed
            addContract(*this, c);
        } else {
            // patrol: somebody wants a risky sector watched
            std::vector<Id> risky;
            for (Id s : near)
                if (systems[s].risk >= 28 && systems[s].zone != Zone::Charter) risky.push_back(s);
            if (risky.empty()) continue;
            Id sys = risky[rng.irange(0, (int)risky.size() - 1)];
            Contract c;
            c.kind = ContractKind::Patrol;
            Id issuer = NONE;
            for (auto& oc : corps)
                if (oc.alive && oc.arch == Arch::Miner && hexDistance(systems[stations[oc.home].system].hex, systems[sys].hex) <= 10) { issuer = oc.id; break; }
            c.issuerName = issuer != NONE ? corps[issuer].name : S.name + " traders' guild";
            c.system = sys;
            c.goal = rng.irange(4, 12) * 3;  // hours on station
            c.reward = roundTo((Money)(c.goal * 80000.0 * (1.0 + systems[sys].risk / 50.0) * CR), 1000 * CR);
            c.expires = now + 3 * MINUTES_PER_DAY;
            c.qty = 5;  // days allowed
            addContract(*this, c);
        }
    }
}

void World::contractsHourly() {
    for (auto& c : contracts) {
        if (c.state != ContractState::Accepted) continue;
        if (c.kind == ContractKind::Patrol) {
            bool on = false;
            if (c.system != NONE)
                for (Id fid : corps[c.contractor].fleetList) {
                    const Fleet& f = fleets[fid];
                    if (!f.alive || f.docked != NONE || f.system != c.system || f.order.kind != OrderKind::Patrol) continue;
                    if (fleetStrength(f) >= 10) { on = true; break; }
                }
            if (on) c.progress++;
        }
        if ((c.kind == ContractKind::Patrol || c.kind == ContractKind::Bounty) && c.progress >= c.goal) {
            c.state = ContractState::Done;
            pay(c.contractor, c.reward, Cat::Contracts, std::string(contractKindName(c.kind)) + " contract: " + c.issuerName);
            if (corps[c.contractor].player && corps[c.contractor].divs[DIV_SECURITY].pol.automate) divBook(c.contractor, DIV_SECURITY, c.reward, 0);
            if (c.issuer != NONE) addStanding(c.issuer, c.contractor, 1.0f);
            if (c.kind == ContractKind::Patrol && c.system != NONE) systems[c.system].heat *= 0.6f;
            if (c.contractor == player) {
                pstats.contractsDone++;
                addLog(Chan::Corp, Sev::Good, true, c.system,
                       std::string(contractKindName(c.kind)) + " contract complete for " + c.issuerName + ": paid " + fmtMoney(c.reward) + ".");
            }
        }
    }
}

Result World::acceptContract(Id corp, Id id) {
    if (id < 0 || id >= (Id)contracts.size()) return Result::fail("Unknown contract.");
    Contract& c = contracts[id];
    if (c.state != ContractState::Open || c.expires <= now) return Result::fail("That contract is no longer open.");
    if (c.issuer == corp) return Result::fail("You posted that contract.");
    if (c.kind == ContractKind::Bounty && c.target == corp) return Result::fail("That bounty is on you.");
    Corp& me = corps[corp];
    int days = (int)(c.kind == ContractKind::Supply || c.kind == ContractKind::Courier ? c.goal : c.qty);
    if (c.kind == ContractKind::Courier) {
        if (me.wallet < c.collateral) return Result::fail("Courier work needs " + fmtMoney(c.collateral) + " collateral, returned on delivery.");
        if (c.issuer == NONE) {
            Station& S = stations[c.from];
            S.stock[c.item] = (float)std::max(0.0, (double)S.stock[c.item] - (double)c.qty);
        }
        pay(corp, -c.collateral, Cat::Contracts, "Collateral held for courier contract");
        addItem(corp, c.from, c.item, c.qty);
    }
    c.contractor = corp;
    c.state = ContractState::Accepted;
    c.deadline = now + days * MINUTES_PER_DAY;
    c.progress = 0;
    std::string what;
    switch (c.kind) {
        case ContractKind::Supply: what = "Deliver " + fmtInt(c.qty) + " " + item(c.item).name + " to " + stations[c.to].name; break;
        case ContractKind::Courier:
            what = fmtInt(c.qty) + " " + item(c.item).name + " are in your hangar at " + stations[c.from].name + ". Deliver them to " +
                   stations[c.to].name;
            break;
        case ContractKind::Bounty: what = "Destroy " + fmtInt(c.goal) + " ships of " + corps[c.target].name; break;
        case ContractKind::Patrol: what = "Patrol " + systems[c.system].name + " with warships for " + fmtInt(c.goal) + " hours"; break;
    }
    if (corp == player) addLog(Chan::Corp, Sev::Info, true, NONE, "Contract accepted. " + what + " within " + std::to_string(days) + " days.");
    return Result::good(what + " within " + std::to_string(days) + " days.");
}

Result World::deliverContract(Id corp, Id id) {
    if (id < 0 || id >= (Id)contracts.size()) return Result::fail("Unknown contract.");
    Contract& c = contracts[id];
    if (c.state != ContractState::Accepted || c.contractor != corp) return Result::fail("That contract is not yours to deliver.");
    if (c.kind != ContractKind::Supply && c.kind != ContractKind::Courier) return Result::fail("That contract completes on its own.");
    int64_t h = have(corp, c.to, c.item);
    if (h < c.qty) return Result::fail("You need " + fmtInt(c.qty) + " " + item(c.item).name + " in your hangar at " + stations[c.to].name + " (have " + fmtInt(h) + ").");
    takeItem(corp, c.to, c.item, c.qty);
    if (c.issuer == NONE) {
        if (stationUsable(c.to)) stations[c.to].stock[c.item] += (float)c.qty;
    } else {
        addItem(c.issuer, c.to, c.item, c.qty);
    }
    Money paid = c.reward + c.collateral;
    pay(corp, paid, Cat::Contracts, std::string(contractKindName(c.kind)) + " contract: " + c.issuerName);
    c.state = ContractState::Done;
    if (c.issuer != NONE) addStanding(c.issuer, corp, 1.0f);
    if (corp == player) pstats.contractsDone++;
    return Result::good("Delivered for " + c.issuerName + ": paid " + fmtMoney(c.reward) + (c.collateral ? " and collateral returned." : "."), c.qty, paid);
}

Result World::postContract(Id corp, const Contract& in) {
    Corp& me = corps[corp];
    Contract c = in;
    c.issuer = corp;
    c.issuerName = me.name;
    c.state = ContractState::Open;
    c.contractor = NONE;
    c.expires = now + 3 * MINUTES_PER_DAY;
    if (c.reward <= 0) return Result::fail("Set a reward.");
    switch (c.kind) {
        case ContractKind::Courier:
            if (!stationUsable(c.from) || !stationUsable(c.to) || c.from == c.to) return Result::fail("Pick two different stations.");
            if (c.item < 0 || isShipItem(c.item) || c.qty <= 0) return Result::fail("Pick the goods and a quantity.");
            if (have(corp, c.from, c.item) < c.qty) return Result::fail("You do not have that many in your hangar at the pickup station.");
            if (me.wallet < c.reward) return Result::fail("The reward is held in escrow: you need " + fmtMoney(c.reward) + ".");
            takeItem(corp, c.from, c.item, c.qty);
            if (c.collateral <= 0) c.collateral = item(c.item).base * c.qty;
            c.goal = std::max<int64_t>(2, c.goal ? c.goal : 5);
            break;
        case ContractKind::Supply:
            if (!stationUsable(c.to) || c.item < 0 || c.qty <= 0) return Result::fail("Pick the station, goods and quantity.");
            if (me.wallet < c.reward) return Result::fail("The reward is held in escrow: you need " + fmtMoney(c.reward) + ".");
            c.goal = std::max<int64_t>(2, c.goal ? c.goal : 5);
            break;
        case ContractKind::Bounty:
            if (c.target < 0 || c.target >= (Id)corps.size() || c.target == corp || !corps[c.target].alive) return Result::fail("Pick a target corporation.");
            if (c.goal <= 0) c.goal = 3;
            if (me.wallet < c.reward) return Result::fail("The reward is held in escrow: you need " + fmtMoney(c.reward) + ".");
            c.qty = 10;
            break;
        case ContractKind::Patrol:
            if (c.system < 0 || c.system >= (Id)systems.size()) return Result::fail("Pick a system.");
            if (c.goal <= 0) c.goal = 24;
            if (me.wallet < c.reward) return Result::fail("The reward is held in escrow: you need " + fmtMoney(c.reward) + ".");
            c.qty = 5;
            break;
    }
    pay(corp, -c.reward, Cat::Contracts, std::string("Reward escrow: ") + contractKindName(c.kind) + " contract");
    Id id = addContract(*this, c);
    Result r = Result::good(std::string(contractKindName(c.kind)) + " contract posted on the board.");
    r.id = id;
    return r;
}

}  // namespace nb

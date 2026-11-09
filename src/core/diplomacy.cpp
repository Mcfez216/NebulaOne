// NebulaOne - wars and diplomacy.
//
// Wars are licensed: the Charter Authority sells a 14-day war licence (from
// 5M cr, or 1.5% of the target's net worth) that takes effect a day later.
// While it runs, fighting between the two is lawful even in Charter space: no
// Wardens, no Charter standing lost. Each side's war score is the value it
// has destroyed. Wars end when the licence runs out or by a peace, often with
// reparations paid by the side that is losing.
//
// Alliances share sensors and defend each other (allied patrols join battles,
// allies are called in when one is attacked). Non-aggression pacts keep two
// corporations' raiders off each other. Mercenary corporations can be hired:
// they take out a licence against your target and send their fleets.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace nb {

const char* pactName(Pact p) {
    switch (p) {
        case Pact::War: return "War";
        case Pact::Alliance: return "Alliance";
        case Pact::NonAggression: return "Non-aggression pact";
    }
    return "?";
}

const char* offerKindName(OfferKind k) {
    switch (k) {
        case OfferKind::Peace: return "Peace";
        case OfferKind::Alliance: return "Alliance";
        case OfferKind::NonAggression: return "Non-aggression pact";
        case OfferKind::JoinWar: return "Call to arms";
    }
    return "?";
}

static bool outlawArch(const Corp& c) { return c.arch == Arch::Pirate || c.arch == Arch::Ganker; }
static constexpr Tick kForever = 3650 * MINUTES_PER_DAY;

static bool canWarTarget(World& w, Id a, Id e) {
    if (e == NONE || e == a || !w.corps[e].alive || outlawArch(w.corps[e]) || w.corps[e].player) return false;
    return !w.allied(a, e) && !w.nonAggression(a, e) && !w.findTreaty(Pact::War, a, e, false);
}

void World::rebuildPacts() {
    pacts.clear();
    for (auto& t : treaties) {
        if (!t.live(now)) continue;
        uint8_t bit = t.kind == Pact::War ? 1 : t.kind == Pact::Alliance ? 2 : 4;
        pacts[pairKey(t.a, t.b)] |= bit;
    }
}

Treaty* World::findTreaty(Pact k, Id a, Id b, bool liveOnly) {
    for (auto& t : treaties) {
        if (!t.active || t.kind != k) continue;
        if (!((t.a == a && t.b == b) || (t.a == b && t.b == a))) continue;
        if (liveOnly && !t.live(now)) continue;
        return &t;
    }
    return nullptr;
}

double World::corpStrength(Id corp) const {
    double s = 0;
    for (Id fid : corps[corp].fleetList)
        if (fleets[fid].alive) s += fleetStrength(fleets[fid]);
    for (Id sid : corps[corp].shipList) {
        const Ship& sh = ships[sid];
        if (sh.alive && sh.fleet == NONE) s += stats(sh).attack * 0.5;  // ships in the hangar count for something
    }
    return s;
}

Money World::warFee(Id target) const {
    return std::max<Money>(5000000 * CR, corps[target].netWorth * 15 / 1000);
}

Money World::mercQuote(Id merc, int days) const {
    double perDay = 150000.0 + corpStrength(merc) * 12000.0;
    return credits(perDay * std::max(1, days));
}

void World::noteAggression(Id victim, Id aggressor) {
    if (victim == NONE || aggressor == NONE || victim == aggressor) return;
    corps[victim].lastAggressor = aggressor;
    corps[victim].lastAggression = now;
}

static Id nextTreatyId(const World& w) { return w.treaties.empty() ? 0 : w.treaties.back().id + 1; }
static Id nextOfferId(const World& w) { return w.offers.empty() ? 0 : w.offers.back().id + 1; }

Result World::declareWar(Id corp, Id target, Id hiredBy, Id onBehalf, const std::string& why) {
    if (corp == target || target < 0 || target >= (Id)corps.size() || !corps[target].alive) return Result::fail("Pick a corporation.");
    Corp& c = corps[corp];
    Corp& t = corps[target];
    if (outlawArch(c)) return Result::fail("Pirates do not buy licences.");
    if (outlawArch(t)) return Result::fail(t.name + " are outlaws already: anyone may fire on them without a licence.");
    if (allied(corp, target) || nonAggression(corp, target)) return Result::fail("You have a pact with " + t.name + ": cancel it first.");
    if (findTreaty(Pact::War, corp, target, false)) return Result::fail("You are already at war with " + t.name + ".");
    Money fee = (hiredBy != NONE || onBehalf != NONE) ? 0 : warFee(target);
    if (c.wallet < fee) return Result::fail("A war licence against " + t.name + " costs " + fmtMoney(fee) + ".");
    if (fee > 0) pay(corp, -fee, Cat::Other, "Charter war licence: " + t.name);
    Treaty w;
    w.id = nextTreatyId(*this);
    w.kind = Pact::War;
    w.a = corp;
    w.b = target;
    w.start = now + (onBehalf != NONE ? 2 * MINUTES_PER_HOUR : hiredBy != NONE ? 6 * MINUTES_PER_HOUR : MINUTES_PER_DAY);
    w.until = w.start + 14 * MINUTES_PER_DAY;
    w.hiredBy = hiredBy;
    w.onBehalf = onBehalf;
    w.why = why;
    treaties.push_back(w);
    const Id warId = w.id;
    const Tick warStart = w.start;
    addStanding(target, corp, -4.0f);
    bool mine = corp == player || target == player || hiredBy == player;
    std::string msg = c.name + " declares war on " + t.name;
    if (!why.empty()) msg += " (" + why + ")";
    msg += ": hostilities from " + fmtDate(w.start) + ".";
    addLog(Chan::News, target == player ? Sev::Bad : Sev::Warn, mine, stations[t.home].system, msg);
    // the target's allies are called to arms
    for (auto& a : corps) {
        if (!a.alive || a.id == corp || a.id == target || !allied(a.id, target)) continue;
        if (a.player) {
            Offer o;
            o.id = nextOfferId(*this);
            o.kind = OfferKind::JoinWar;
            o.from = target;
            o.to = a.id;
            o.treaty = warId;
            o.expires = now + 2 * MINUTES_PER_DAY;
            o.note = t.name + " asks you to join its war against " + c.name + ".";
            offers.push_back(o);
            addLog(Chan::Corp, Sev::Warn, true, NONE, o.note + " (Diplomacy window)");
        } else if (standing(a.id, target) >= 4.0f && rng.chance(0.75) && !allied(a.id, corp) && !nonAggression(a.id, corp)) {
            declareWar(a.id, corp, NONE, target, "in defence of " + t.name);
        }
    }
    Result r = Result::good("War declared on " + t.name + ". The licence takes effect " + fmtDate(warStart) + " and runs 14 days.", 0, fee);
    r.id = warId;
    return r;
}

bool World::aiAcceptsPact(Id ai, Id other, Pact kind, std::string* why) const {
    const Corp& a = corps[ai];
    auto no = [&](const char* r) { if (why) *why = r; return false; };
    if (outlawArch(a)) return no("pirates sign nothing");
    if (atWar(ai, other)) return no("you are at war");
    float s = standing(ai, other);
    int count = 0;
    for (auto& t : treaties)
        if (t.active && t.kind == kind && (t.a == ai || t.b == ai)) count++;
    if (kind == Pact::NonAggression) {
        if (s < 0.0f) return no("they do not trust you (standing below 0)");
        if (count >= 2) return no("they have enough pacts");
        return true;
    }
    if (count >= 3) return no("they have all the allies they want");
    // a common enemy: someone either of them is at war with that the other also fights or detests
    bool commonEnemy = false;
    for (auto& t : treaties) {
        if (!t.live(now) || t.kind != Pact::War) continue;
        for (Id side : {ai, other}) {
            Id e = t.a == side ? t.b : t.b == side ? t.a : NONE;
            if (e == NONE) continue;
            Id them = side == ai ? other : ai;
            if (atWar(them, e) || standing(them, e) <= -3.0f) commonEnemy = true;
        }
    }
    if (s >= 6.0f || (s >= 1.0f && commonEnemy)) return true;
    return no(commonEnemy ? "your standing with them is too low (1 with a common enemy)" : "they see no common enemy, and your standing is under 6");
}

Result World::proposePact(Id corp, Id target, Pact kind) {
    if (kind == Pact::War) return declareWar(corp, target);
    if (corp == target || target < 0 || target >= (Id)corps.size() || !corps[target].alive) return Result::fail("Pick a corporation.");
    if (findTreaty(kind, corp, target, false)) return Result::fail("You already have that pact.");
    if (atWar(corp, target) || findTreaty(Pact::War, corp, target, false)) return Result::fail("Make peace first.");
    std::string why;
    if (!corps[target].player && !aiAcceptsPact(target, corp, kind, &why)) return Result::fail(corps[target].name + " declined: " + why + ".");
    Treaty t;
    t.id = nextTreatyId(*this);
    t.kind = kind;
    t.a = corp;
    t.b = target;
    t.start = now;
    t.until = now + kForever;
    treaties.push_back(t);
    rebuildPacts();
    if (kind == Pact::Alliance) updateSight();
    addStanding(corp, target, 1.0f);
    addStanding(target, corp, 1.0f);
    bool mine = corp == player || target == player;
    addLog(Chan::News, Sev::Info, mine, NONE, corps[corp].name + " and " + corps[target].name + " sign a " + std::string(kind == Pact::Alliance ? "defensive alliance" : "non-aggression pact") + ".");
    return Result::good(std::string(pactName(kind)) + " signed with " + corps[target].name + ".");
}

bool World::aiAcceptsPeace(Id ai, Id other, Money paidToAi, std::string* why) const {
    const Treaty* w = nullptr;
    for (auto& t : treaties)
        if (t.active && t.kind == Pact::War && ((t.a == ai && t.b == other) || (t.a == other && t.b == ai))) w = &t;
    if (!w) { if (why) *why = "you are not at war"; return true; }
    double mine = toCr(w->a == ai ? w->scoreA : w->scoreB), theirs = toCr(w->a == ai ? w->scoreB : w->scoreA);
    double age = (double)(now - w->start) / MINUTES_PER_DAY;
    if (paidToAi < 0) {
        double owe = toCr(-paidToAi);
        bool beaten = theirs > 2.0 * mine + 3e6;
        double cap = std::min(toCr(corps[ai].wallet) * 0.15, (theirs - mine) * 0.3);
        if (beaten && owe <= cap) return true;
        if (why) *why = beaten ? "they will pay at most " + fmtMoneyShort(credits(std::max(0.0, cap))) : "they are not beaten";
        return false;
    }
    double demand = mine > theirs ? (mine - theirs) * 0.25 : 0;
    if (toCr(paidToAi) >= demand && (age >= 2.0 || toCr(paidToAi) >= demand * 1.5 + 1e6)) return true;
    if (why) *why = age < 2.0 && toCr(paidToAi) >= demand ? "the war is too young; try again in a day or two"
                                                         : "they want at least " + fmtMoneyShort(credits(demand)) + " in reparations";
    return false;
}

void World::makePeace(Treaty& war, Money paidByA, const std::string& how) {
    Id payer = paidByA >= 0 ? war.a : war.b, payee = paidByA >= 0 ? war.b : war.a;
    Money amt = std::min<Money>(paidByA < 0 ? -paidByA : paidByA, std::max<Money>(0, corps[payer].wallet));
    if (amt > 0) {
        pay(payer, -amt, Cat::Other, "Reparations to " + corps[payee].name);
        pay(payee, amt, Cat::Other, "Reparations from " + corps[payer].name);
    }
    if (amt > 0 && payee == player) pstats.warsWon++;
    std::string tail = amt > 0 ? "; " + corps[payer].name + " pays " + fmtMoneyShort(amt) + " in reparations" : "";
    endTreaty(war, how + tail);
    addStanding(war.a, war.b, 1.0f);
    addStanding(war.b, war.a, 1.0f);
}

Result World::proposePeace(Id corp, Id target, Money amount) {
    Treaty* w = findTreaty(Pact::War, corp, target, false);
    if (!w) return Result::fail("You are not at war with " + corps[target].name + ".");
    if (w->start > now && w->a == corp && w->hiredBy == NONE) {
        endTreaty(*w, "the declaration was withdrawn before it took effect");
        return Result::good("Declaration withdrawn. The licence fee is not refunded.");
    }
    if (amount > 0 && corps[corp].wallet < amount) return Result::fail("You do not have " + fmtMoney(amount) + ".");
    std::string why;
    if (!corps[target].player && !aiAcceptsPeace(target, corp, amount, &why)) return Result::fail(corps[target].name + " refused: " + why + ".");
    Money paidByA = w->a == corp ? amount : -amount;
    makePeace(*w, paidByA, "peace agreed");
    return Result::good("Peace with " + corps[target].name + ".");
}

Result World::respondOffer(Id corp, Id offerId, bool accept) {
    Offer* o = nullptr;
    for (auto& x : offers)
        if (x.id == offerId) o = &x;
    if (!o || !o->open || o->to != corp) return Result::fail("That offer is no longer open.");
    o->open = false;
    if (o->expires <= now) return Result::fail("That offer has expired.");
    if (!accept) {
        if (o->kind == OfferKind::JoinWar) addStanding(o->from, corp, -1.0f);
        return Result::good("Declined.");
    }
    switch (o->kind) {
        case OfferKind::Peace: {
            Treaty* w = findTreaty(Pact::War, o->from, o->to, false);
            if (!w) return Result::fail("The war is already over.");
            if (o->amount < 0 && corps[corp].wallet < -o->amount) return Result::fail("You cannot pay " + fmtMoney(-o->amount) + ".");
            Money paidByA = w->a == o->from ? o->amount : -o->amount;
            makePeace(*w, paidByA, "peace agreed");
            return Result::good("Peace with " + corps[o->from].name + ".");
        }
        case OfferKind::Alliance:
        case OfferKind::NonAggression: {
            Pact k = o->kind == OfferKind::Alliance ? Pact::Alliance : Pact::NonAggression;
            if (findTreaty(k, o->from, o->to, false)) return Result::fail("Already signed.");
            Treaty t;
            t.id = nextTreatyId(*this);
            t.kind = k;
            t.a = o->from;
            t.b = o->to;
            t.start = now;
            t.until = now + kForever;
            treaties.push_back(t);
            rebuildPacts();
            updateSight();
            addLog(Chan::News, Sev::Info, true, NONE, corps[o->from].name + " and " + corps[o->to].name + " sign a " + pactName(k) + ".");
            return Result::good(std::string(pactName(k)) + " signed.");
        }
        case OfferKind::JoinWar: {
            Treaty* w = nullptr;
            for (auto& t : treaties)
                if (t.id == o->treaty && t.active) w = &t;
            if (!w) return Result::fail("That war is over.");
            Id enemy = w->a == o->from ? w->b : w->a;
            return declareWar(corp, enemy, NONE, o->from, "in defence of " + corps[o->from].name);
        }
    }
    return Result::fail("?");
}

Result World::cancelPact(Id corp, Id treatyId) {
    for (auto& t : treaties) {
        if (t.id != treatyId || !t.active || (t.a != corp && t.b != corp)) continue;
        Id other = t.a == corp ? t.b : t.a;
        if (t.kind == Pact::War) {
            if (t.start > now && t.a == corp) { endTreaty(t, "the declaration was withdrawn"); return Result::good("Declaration withdrawn."); }
            return Result::fail("Wars end with a peace or when the licence runs out.");
        }
        if (t.until <= now + MINUTES_PER_DAY) return Result::fail("Already ending.");
        t.until = now + MINUTES_PER_DAY;
        addStanding(other, corp, -2.0f);
        addLog(Chan::News, Sev::Info, corp == player || other == player, NONE,
               corps[corp].name + " gives notice to end its " + pactName(t.kind) + " with " + corps[other].name + " (ends in 24 hours).");
        return Result::good("Notice given: the pact ends in 24 hours.");
    }
    return Result::fail("Pact not found.");
}

Result World::hireMercs(Id corp, Id merc, Id target, int days) {
    if (merc < 0 || merc >= (Id)corps.size() || !corps[merc].alive || corps[merc].arch != Arch::Mercenary)
        return Result::fail("Pick a mercenary corporation.");
    if (target == merc || target == corp || target < 0 || target >= (Id)corps.size() || !corps[target].alive) return Result::fail("Pick a target.");
    if (outlawArch(corps[target])) return Result::fail("Mercenaries hunt outlaws anyway: post a bounty instead.");
    if (findTreaty(Pact::War, merc, target, false)) return Result::fail(corps[merc].name + " is already at war with " + corps[target].name + ".");
    if (allied(merc, target) || nonAggression(merc, target)) return Result::fail(corps[merc].name + " has a pact with " + corps[target].name + ".");
    if (allied(corp, target) || nonAggression(corp, target)) return Result::fail("You have a pact with " + corps[target].name + ".");
    days = std::clamp(days, 3, 30);
    Money q = mercQuote(merc, days);
    if (corps[corp].wallet < q) return Result::fail(corps[merc].name + " wants " + fmtMoney(q) + " for " + std::to_string(days) + " days.");
    pay(corp, -q, Cat::Contracts, "Mercenary contract: " + corps[merc].name + " against " + corps[target].name);
    pay(merc, q, Cat::Contracts, "Mercenary contract from " + corps[corp].name);
    Result r = declareWar(merc, target, corp, NONE, "hired by " + corps[corp].name);
    if (!r.ok) {
        pay(merc, -q, Cat::Contracts, "Refund");
        pay(corp, q, Cat::Contracts, "Refund");
        return r;
    }
    Tick start = now;
    for (auto& t : treaties)
        if (t.id == r.id) { t.until = t.start + days * MINUTES_PER_DAY; start = t.start; }
    addStanding(target, corp, -3.0f);
    return Result::good(corps[merc].name + " takes the contract: war on " + corps[target].name + " from " + fmtDate(start) + " for " +
                        std::to_string(days) + " days.");
}

void World::endTreaty(Treaty& t, const std::string& why) {
    if (!t.active) return;
    t.active = false;
    t.until = std::min(t.until, now);
    bool mine = t.a == player || t.b == player || t.hiredBy == player;
    std::string what = t.kind == Pact::War ? "war between " : std::string(pactName(t.kind)) + " between ";
    std::string score;
    if (t.kind == Pact::War && (t.scoreA > 0 || t.scoreB > 0))
        score = " Destroyed: " + corps[t.a].ticker + " " + fmtMoneyShort(t.scoreA) + ", " + corps[t.b].ticker + " " + fmtMoneyShort(t.scoreB) + ".";
    std::string w = what + corps[t.a].name + " and " + corps[t.b].name;
    w[0] = (char)std::toupper((unsigned char)w[0]);
    addLog(Chan::News, Sev::Info, mine, NONE, w + " is over: " + why + "." + score);
    // the end of a war frees the fleets sent to fight it
    for (Id side : {t.a, t.b})
        for (Id fid : corps[side].fleetList) {
            Fleet& f = fleets[fid];
            if (f.alive && !corps[side].player && f.order.targetCorp != NONE && (f.order.targetCorp == t.a || f.order.targetCorp == t.b)) setOrder(f, FleetOrder{});
        }
    rebuildPacts();
    updateSight();
}

void World::diplomacyHourly() {
    bool changed = false;
    for (auto& t : treaties) {
        if (!t.active) continue;
        if (t.kind == Pact::War && t.start <= now && t.start > now - 60) {
            changed = true;
            bool mine = t.a == player || t.b == player || t.hiredBy == player;
            addLog(Chan::News, mine ? Sev::Bad : Sev::Warn, mine, stations[corps[t.b].home].system,
                   "War: " + corps[t.a].name + " and " + corps[t.b].name + " are now at war. Fighting between them is lawful everywhere.");
        }
        if (t.until <= now) {
            // a winning aggressor may renew its licence
            if (t.kind == Pact::War && !corps[t.a].player && t.hiredBy == NONE && t.scoreA > t.scoreB * 3 / 2 + 2000000 * CR &&
                corps[t.a].wallet > warFee(t.b) * 3 && rng.chance(0.5)) {
                pay(t.a, -warFee(t.b), Cat::Other, "War licence renewed: " + corps[t.b].name);
                t.until = now + 14 * MINUTES_PER_DAY;
                addLog(Chan::News, Sev::Warn, t.b == player, NONE, corps[t.a].name + " renews its war licence against " + corps[t.b].name + ".");
                continue;
            }
            endTreaty(t, t.kind == Pact::War ? "the licence ran out" : "notice served");
            changed = true;
        }
    }
    for (auto& o : offers)
        if (o.open && o.expires <= now) o.open = false;
    if (changed) { rebuildPacts(); updateSight(); }
}

void World::diplomacyDaily() {
    // forget closed business
    treaties.erase(std::remove_if(treaties.begin(), treaties.end(), [&](const Treaty& t) { return !t.active && now - t.until > 30 * MINUTES_PER_DAY; }),
                   treaties.end());
    offers.erase(std::remove_if(offers.begin(), offers.end(), [&](const Offer& o) { return !o.open && now - o.expires > 7 * MINUTES_PER_DAY; }),
                 offers.end());
    for (auto& c : corps)
        if (c.alive && !c.player && !outlawArch(c)) aiDiplomacy(c);
}

void World::aiDiplomacy(Corp& c) {
    int aiWars = 0;
    for (auto& t : treaties)
        if (t.active && t.kind == Pact::War && !corps[t.a].player && !corps[t.b].player) aiWars++;
    int warCap = std::max(2, (int)corps.size() / 12);
    double mine = corpStrength(c.id);

    // ---- the wars it is in: settle the ones going badly
    for (auto& t : treaties) {
        if (!t.live(now) || t.kind != Pact::War || (t.a != c.id && t.b != c.id) || t.hiredBy != NONE) continue;
        Id e = t.a == c.id ? t.b : t.a;
        double my = toCr(t.a == c.id ? t.scoreA : t.scoreB), their = toCr(t.a == c.id ? t.scoreB : t.scoreA);
        double age = (double)(now - t.start) / MINUTES_PER_DAY;
        if (age < 2) continue;
        bool losing = their > 2.0 * my + 3e6;
        bool even = std::fabs(their - my) < 0.25 * std::max(1e6, std::max(my, their));
        bool openOffer = false;
        for (auto& o : offers)
            if (o.open && o.kind == OfferKind::Peace && o.from == c.id && o.to == e) openOffer = true;
        if (corps[e].player) {
            if (openOffer) continue;
            Money amt = 0;
            if (losing && rng.chance(0.5)) amt = credits(std::min(toCr(c.wallet) * 0.1, (their - my) * 0.2));
            else if (my > 2.0 * their + 3e6 && rng.chance(0.3)) amt = -credits((my - their) * 0.25);
            else if (even && age > 6 && rng.chance(0.25)) amt = 0;
            else continue;
            Offer o;
            o.id = nextOfferId(*this);
            o.kind = OfferKind::Peace;
            o.from = c.id;
            o.to = e;
            o.treaty = t.id;
            o.amount = amt;
            o.expires = now + 2 * MINUTES_PER_DAY;
            o.note = amt > 0 ? c.name + " sues for peace and offers " + fmtMoneyShort(amt) + " in reparations."
                     : amt < 0 ? c.name + " offers peace if you pay " + fmtMoneyShort(-amt) + "."
                               : c.name + " proposes a white peace.";
            offers.push_back(o);
            addLog(Chan::Corp, Sev::Warn, true, NONE, o.note + " (Diplomacy window)");
        } else if (losing && rng.chance(0.4)) {
            Money amt = credits(std::min(toCr(c.wallet) * 0.1, (their - my) * 0.2));
            makePeace(t, t.a == c.id ? amt : -amt, c.name + " sued for peace");
            return;
        } else if (even && age > 7 && rng.chance(0.25)) {
            makePeace(t, 0, "a white peace");
            return;
        }
    }

    // ---- new wars
    auto canWar = [&](Id e) {
        if (e == NONE || e == c.id || !corps[e].alive || outlawArch(corps[e])) return false;
        if (allied(c.id, e) || nonAggression(c.id, e) || findTreaty(Pact::War, c.id, e, false)) return false;
        return c.wallet > warFee(e) * 3 && mine >= 0.6 * corpStrength(e) && mine > 20;
    };
    Id aggr = c.lastAggressor;
    if (aggr != NONE && now - c.lastAggression < 3 * MINUTES_PER_DAY) {
        if (canWar(aggr) && (corps[aggr].player ? rng.chance(0.6) : aiWars < warCap && rng.chance(0.5))) {
            declareWar(c.id, aggr, NONE, NONE, "retaliation");
            c.lastAggressor = NONE;
            return;
        }
        // too weak to fight: hire a mercenary company if it can afford one
        if (mine < 0.6 * corpStrength(aggr) && !outlawArch(corps[aggr]) && rng.chance(0.3)) {
            for (auto& m : corps) {
                if (!m.alive || m.arch != Arch::Mercenary || m.id == aggr) continue;
                Money q = mercQuote(m.id, 7);
                if (c.wallet > q * 4 && hireMercs(c.id, m.id, aggr, 7).ok) { c.lastAggressor = NONE; return; }
                break;
            }
        }
    }
    if (rng.chance(0.03)) {
        // grudges, and rivalry over Frontier sovereignty
        Id homeSys = stations[c.home].system;
        for (Id sy : systemsWithin(homeSys, 6)) {
            Id o = systems[sy].owner;
            Id e = o != NONE && o != c.id ? o : NONE;
            if (e == NONE) {
                for (Id fid : fleetsIn[sy])
                    if (fleets[fid].corp != c.id && standing(c.id, fleets[fid].corp) <= -6.0f) { e = fleets[fid].corp; break; }
            }
            if (e == NONE || !canWar(e)) continue;
            bool rival = o == e && systems[homeSys].owner == c.id;
            if (corps[e].player ? standing(c.id, e) <= -6.0f : (aiWars < warCap && (rival || standing(c.id, e) <= -6.0f))) {
                declareWar(c.id, e, NONE, NONE, rival ? "a dispute over Frontier sovereignty" : "a long-standing grudge");
                return;
            }
        }
    }

    // ---- commercial rivalry: rich corporations hire mercenaries against a competitor nearby
    if ((c.arch == Arch::Trader || c.arch == Arch::Industrialist || c.arch == Arch::Miner) && aiWars < warCap && rng.chance(0.004)) {
        Id homeSys = stations[c.home].system;
        Id rival = NONE;
        Money best = 0;
        for (auto& o : corps)
            if (o.alive && o.id != c.id && o.arch == c.arch && o.netWorth > best && canWarTarget(*this, c.id, o.id) &&
                hexDistance(systems[homeSys].hex, systems[stations[o.home].system].hex) <= 25) {
                best = o.netWorth;
                rival = o.id;
            }
        if (rival != NONE) {
            if (c.arch == Arch::Miner && mine >= 60 && mine >= corpStrength(rival) && c.wallet > warFee(rival) * 3) {
                declareWar(c.id, rival, NONE, NONE, "a quarrel over ore fields");
                return;
            }
            for (auto& m : corps) {
                if (!m.alive || m.arch != Arch::Mercenary || m.id == rival || rng.chance(0.5)) continue;
                if (c.wallet > mercQuote(m.id, 7) * 4 && hireMercs(c.id, m.id, rival, 7).ok) return;
            }
        }
    }

    // ---- pacts
    if (rng.chance(0.015)) {
        Id homeSys = stations[c.home].system;
        std::vector<Id> near;
        for (auto& o : corps)
            if (o.alive && o.id != c.id && !outlawArch(o) && hexDistance(systems[homeSys].hex, systems[stations[o.home].system].hex) <= 30)
                near.push_back(o.id);
        if (near.empty()) return;
        Id o = near[rng.irange(0, (int)near.size() - 1)];
        if (findTreaty(Pact::Alliance, c.id, o, false) || findTreaty(Pact::War, c.id, o, false)) return;
        bool haveNap = findTreaty(Pact::NonAggression, c.id, o, false) != nullptr;
        Pact k = aiAcceptsPact(c.id, o, Pact::Alliance, nullptr) ? Pact::Alliance : Pact::NonAggression;
        if (k == Pact::NonAggression && (haveNap || standing(c.id, o) < (corps[o].player ? 2.0f : 0.0f))) return;
        if (corps[o].player) {
            for (auto& x : offers)
                if (x.open && x.from == c.id && x.to == o) return;
            Offer of;
            of.id = nextOfferId(*this);
            of.kind = k == Pact::Alliance ? OfferKind::Alliance : OfferKind::NonAggression;
            of.from = c.id;
            of.to = o;
            of.expires = now + 3 * MINUTES_PER_DAY;
            of.note = c.name + " proposes a " + (k == Pact::Alliance ? std::string("defensive alliance") : std::string("non-aggression pact")) + ".";
            offers.push_back(of);
            addLog(Chan::Corp, Sev::Good, true, NONE, of.note + " (Diplomacy window)");
        } else if (aiAcceptsPact(o, c.id, k, nullptr)) {
            if (haveNap && k == Pact::Alliance)
                if (Treaty* n = findTreaty(Pact::NonAggression, c.id, o, false)) n->active = false;  // the alliance supersedes it
            proposePact(c.id, o, k);
        }
    }
}

// Send combat fleets after the enemy: its outposts first, then its home system.
void World::aiWar(Corp& c) {
    std::vector<Id> enemies;
    for (auto& t : treaties) {
        if (!t.live(now) || t.kind != Pact::War) continue;
        if (t.a == c.id) enemies.push_back(t.b);
        else if (t.b == c.id) enemies.push_back(t.a);
    }
    if (enemies.empty()) return;
    std::vector<Fleet*> pool;
    int busy = 0;
    for (Id fid : c.fleetList) {
        Fleet& f = fleets[fid];
        if (!f.alive || f.battle != NONE || fleetStrength(f) < 10) continue;
        if (f.order.targetCorp != NONE && atWar(c.id, f.order.targetCorp)) { busy++; continue; }
        pool.push_back(&f);
    }
    int want = std::max(1, (int)(pool.size() + busy) * 2 / 3) - busy;
    for (int k = 0; k < want && k < (int)pool.size(); k++) {
        Fleet& f = *pool[k];
        Id e = enemies[rng.irange(0, (int)enemies.size() - 1)];
        FleetOrder o;
        o.targetCorp = e;
        o.policy = RoutePolicy::Shortest;
        Id best = NONE;
        int bd = 16;
        for (Id oid : outpostIds) {
            const Station& st = stations[oid];
            if (st.dead || st.owner != e || st.state == OutpostState::Anchoring) continue;
            int d = jumps(f.system, st.system);
            if (d < bd) { bd = d; best = oid; }
        }
        if (best != NONE && fleetStrength(f) >= 30) {
            o.kind = OrderKind::Siege;
            o.station = best;
            o.system = stations[best].system;
        } else {
            Id hs = stations[corps[e].home].system;
            if (jumps(f.system, hs) > 22) continue;
            o.kind = OrderKind::Patrol;
            o.system = hs;
        }
        setOrder(f, o);
    }
}

}  // namespace nb

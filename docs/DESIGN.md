# NebulaOne — Game Design Document

Exported from the NebulaOne design document for version 0.4.0, 9 November 2026. Diagrams live in the original document.

## Vision and pillars

You are the CEO of a far-future corporation: you never fly a ship, you run the company that owns them. NebulaOne is a "spreadsheets in space" sandbox where every price, route and war comes from corporations acting under the same rules.

- **One economy, no fake money.** Every item is mined, refined, built, hauled and consumed by someone. Prices come from order books, not lookup tables.
- **Risk pays.** Charter space is safe and thin; the Frontier is lawless and rich; the underwriters price everything in between. Every route and every belt is a risk/reward call.
- **Run the company, not the ship.** You give standing orders to fleets and staff, then watch a live hex chart, a price tape and a ledger.
- **Same rules for everyone.** AI corporations issue the same commands the player does, so multiplayer later only changes who issues them.
- **Spreadsheets first.** Every screen is a sortable, filterable table of numbers you can trust; charts appear only where the shape matters.

Version 0.2 gave NebulaOne its own law (three zones with live risk ratings) and drew the universe as a hex sector chart. Version 0.3 brings back see-through panels over deep space, tinted in your corporation's livery colour, and adds fog of war, exploration, licensed wars and diplomacy, and a galaxy of events. Version 0.4 lets directors run a large company and adds construction and megaprojects, planets and colonies, capital markets, and a window layout that is easy to dock, float and reset. Out of scope for now: flying ships yourself, 3D graphics, and multiplayer (planned, see the roadmap).

## The universe

Each game generates its own hex sector chart from a seed, at one of five sizes. Systems sit on a flat-topped hex grid, never on neighbouring hexes, and travel time is a lane's length in hexes times the slowest ship's minutes per hex.

| Chart | Systems | AI corporations | Charter cores | Regions | Meant for |
| --- | --- | --- | --- | --- | --- |
| Compact | 80 | 12 | 1 | about 4 | the tutorial, a short game |
| Standard | 400 | 40 | 3 | about 18 | the default |
| Vast | 2,000 | 120 | 8 | about 90 | long campaigns |
| Galaxy | 8,000 | 300 | 24 | about 340 | the size of the largest space MMOs |
| Expanse | 20,000 | 600 | 56 | about 830 | experimental; needs a fast machine at top speed |

- **Zones.** Systems are ranked by hex distance from the nearest Charter core, wobbled by noise: the nearest 30% are Charter space, the next 35% Contract space, the rest the Frontier. Every empty hex takes the zone of its nearest system, so the chart reads as territory.
- **Depth** 0 to 3 runs with distance (core Charter, outer Charter and inner Contract, outer Contract, Frontier) and sets which ores the belts hold and how big they are.
- **Regions** grow across the hex grid from scattered seeds, about one per 22 systems. Each region with lawful space gets a trade hub at its best-connected Charter system.
- **Lanes** start as a spanning tree over neighbours up to 4 hexes apart, then bridges join any islands, then extra lanes of up to 3 hexes make loops: never crossing, at most 4 per system, and rarer across zone borders, so borders have chokepoints.
- **Stations.** Charter systems have 1 to 3, Contract systems 0 to 2, the Frontier none until someone anchors an outpost (bigger charts start with a few AI outposts). Settled systems get generated names; Frontier systems carry survey designations such as K4-X9P; every system has a column-and-row hex id such as 0412 or 128.120.

## Economy and production chains

Value flows from rocks to ships: ore is refined into minerals, minerals are built into ships, and ships are lost and replaced. A second business runs alongside: station populations produce and consume trade goods, which gives haulers and traders work that has nothing to do with mining.

*[Diagram: production chains · mining, ships and trade goods]*

Ships destroyed by pirates and gankers are the sink that keeps the whole chain paying, from the belt to the shipyard.

**Ores.** Ore is 1 m³ per unit and is refined in batches of 100. Richer ores sit further from safety; a unit of Aetherite is worth about 17 times a unit of Cindrite.

| Ore | Common at depth | Refines into (per 100 units, perfect yield) | Value per unit at perfect yield |
| --- | --- | --- | --- |
| Cindrite | 0 (core Charter) | 400 Ferrox | 20 cr |
| Pallasite | 0 (core Charter) | 250 Ferrox, 120 Silvane | 27 cr |
| Corvane | 1 (outer Charter, inner Contract) | 200 Silvane, 90 Coriel | 51 cr |
| Vantrite | 1 (outer Charter, inner Contract) | 150 Coriel, 40 Vantium | 81 cr |
| Luminar | 2 (outer Contract) | 60 Vantium, 25 Lucenite | 117 cr |
| Zyrkon | 2 (outer Contract) | 40 Lucenite, 12 Zyrium | 184 cr |
| Noctyx | 3 (the Frontier) | 20 Zyrium, 6 Nocturnite | 260 cr |
| Aetherite | 3 (the Frontier) | 8 Nocturnite, 3 Aetherium | 340 cr |

**Minerals** (0.01 m³ per unit), base prices: Ferrox 5 cr, Silvane 12 cr, Coriel 30 cr, Vantium 90 cr, Lucenite 250 cr, Zyrium 700 cr, Nocturnite 2,000 cr, Aetherium 6,000 cr. Base prices are only anchors; real prices float with supply and demand.

**Trade goods** are made and eaten by station populations, never by players directly.

| Good | Volume | Base price | Made at | Most wanted at |
| --- | --- | --- | --- | --- |
| Ration Packs | 0.5 m³ | 40 cr | Agricultural | Everywhere, Mining Colonies most |
| Fuel Cells | 1.0 m³ | 120 cr | Industrial | Everywhere, Military most |
| Medical Kits | 0.2 m³ | 300 cr | Research | Contract space and Military |
| Electronics | 0.5 m³ | 650 cr | Industrial, Research | Trade Hubs, Military |
| Luxury Goods | 0.3 m³ | 1,800 cr | Trade Hubs | Contract-space stations |

**Components** are the middle of the chain: Industrial stations make some and their shipyards eat them, so a corporation that builds its own from minerals has a margin to take. Every hull, module and the Outpost Kit is made from components.

| Component | One run of 10 needs | Base price per unit |
| --- | --- | --- |
| Hull Plating | 4,000 Ferrox, 600 Silvane | 3,046 cr |
| Thruster Assembly | 1,000 Ferrox, 400 Coriel, 80 Vantium | 2,710 cr |
| Power Core | 100 Vantium, 60 Lucenite, 10 Zyrium | 3,472 cr |
| Sensor Array | 800 Silvane, 40 Lucenite, 10 Electronics | 2,923 cr |
| Weapon Mount | 2,000 Ferrox, 300 Coriel, 120 Vantium | 3,338 cr |
| Extraction Head | 1,500 Ferrox, 500 Coriel, 30 Lucenite | 3,360 cr |

Stations also buy minerals (Industrial, Military, hubs, outposts), buy and sell components, and sell hulls and modules (hubs, Industrial and Military stations). That keeps a floor under mining income and a ceiling on ship prices, while players and AI corps compete inside those bounds. Component and module flows are deliberately thin: a station consumes about 40 hull plating a day, so building to order pays and flooding a market does not.

## Markets

Every station runs its own order book; there is no global price, so arbitrage between stations is a real business. Players and AI corporations trade on the same books with the same fees.

- **Orders.** Each order has an owner, item, side (buy or sell), price, quantity and a 30-day expiry. The book is sorted best price first.
- **Instant trades.** Buying takes from the cheapest sell orders; selling fills the highest buy orders. Large trades walk the book and move the price.
- **Limit orders.** Your order sits in the book until someone fills it or it expires. Escrow: buy orders lock the credits, sell orders lock the goods in that station's hangar.
- **Fees.** Broker fee 2.5% of order value when placing a limit order; sales tax 3% on every sale. Standings with the station owner and a good CFO reduce both.
- **History.** Each station keeps daily open, high, low, close and volume per item. The Market window charts it and shows the live book as a depth table.

**Station market makers.** Each station's population acts as a market maker. Every game hour it cancels and re-posts its own orders: sells from what it holds, buys what it is short of. Its reference price for an item is:

```latex
P = P_{base} \times \mathrm{clamp}\left(\left(\frac{S_{target}}{S_{stock}}\right)^{k},\ lo,\ hi\right) \times (1 + r_{tier})
```

Here S is the station's stock against the level it wants, and r is a remoteness premium: 0% in Charter space, 12% in Contract space and 30% in the Frontier, plus 0.1% per point of the system's risk rating. The exponent k is 0.5 (0.4 for ships); the clamp is 0.35 to 3x for minerals, 0.4 to 3x for goods, 0.4 to 1.25x for ore and 0.45 to 2.5x for components, 0.7 to 2x for modules and 0.8 to 1.6x for ships and blueprint copies. Producers slow down once their stock passes 1.5x target, and every stock drifts back toward target by 5 to 15% a day. Sell orders sit a few percent above P and buy orders a few percent below, in a ladder of 3 to 5 price steps. Shortages raise prices for everyone, and dumping cargo into a thin market crashes them.

**Markets at scale.** A station near activity (a fleet in its system in the last six hours, any trade, or a trade hub) posts its ladder as real orders and updates hourly. Every other station updates every six hours and quotes its ladder on demand, computed from the same formula, so a Galaxy of 7,800 stations costs almost nothing until a fleet arrives and the quotes become orders. Daily price history is kept for 90 days at hubs and 45 days elsewhere, and only where corporations have traded in the last month.

## Corporation management

The corporation is the player's only avatar: a wallet, a ledger, hangars full of assets at specific stations, staff, and standings with everyone else. Net worth is the score.

- **Wallet.** Credits (cr). Every movement of money is a ledger line with a date, amount, counterparty and category.
- **Ledger categories.** Market sales, market purchases, broker fees, sales tax, refining fees, manufacturing fees, wages, loan interest and principal, dividends, insurance premiums and payouts, ship losses.
- **Reports.** A daily income statement (by category, with 7- and 30-day columns) and a balance sheet: cash, inventory at station mid price, ships at replacement cost, shares at market, loans receivable minus loans payable.
- **Assets live somewhere.** Items sit in a hangar at one station. You can only refine, build or sell what is physically at that station, so logistics is half the game.
- **Standings** run from -10 to +10 with each AI corporation, and a separate Charter standing with the Charter Authority. They change with trade, contracts, attacks and loan defaults. Charter standing lowers fees and raises refinery yield; at -5 or below you are an outlaw: mercenaries hunt you, the Authority posts bounties on you, and whoever kills your ships is paid for it.
- **Starting position.** 1.5 million cr, one Mattock mining frigate, one Drover hauler and two hired pilots, docked at the trade hub of the first Charter core.

## Fleets, hauling and logistics

Ships are grouped into fleets, and a fleet runs one standing order until you change it. You manage outcomes, not flight paths: a mining fleet keeps mining and unloading for days without attention.

| Order | What the fleet does |
| --- | --- |
| Move | Travel to a station and dock |
| Mine | Fly to a belt, mine until the ore holds are full, unload at a chosen station, repeat |
| Haul | Load named items at station A, carry them to station B; once or on a loop |
| Trade run | Buy an item at A up to a price limit and budget, sell it at B (the AI traders' bread and butter) |
| Patrol | Hold a system, engage hostiles, deter attacks on friendly fleets passing through |
| Raid | Lurk in a system and attack passing convoys (piracy) |
| Siege | Sit in an outpost's system and wear down its shield, then (after a 24-hour reinforcement) its hull; the owner treats you as an enemy from the first hour |
| Survey | Scan a system for hidden sites; optionally carry on to the next unsurveyed system within 3 jumps |
| Salvage | Work a derelict or relic cache your surveys found, then carry the haul to a station |
| Scout | Hold a system as a picket: with fog of war on, the fleet's sensors reach one jump further there |

Seven hulls, each with fitting slots. Travel time per lane = minutes per hex x the lane's length in hexes (2 to 4 for most lanes), set by the slowest ship and its pilot's Navigation.

| Hull | Class | Cargo m3 | Ore hold m3 | Mining /h | Attack | Hull | Min per hex | Slots | Price |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Mattock | Mining frigate | 150 | 2,500 | 600 | 2 | 20 | 5.0 | 2 | 334k cr |
| Quarrier | Mining barge | 300 | 14,000 | 2,600 | 3 | 60 | 8.0 | 3 | 2.69M cr |
| Drover | Light hauler | 6,000 | - | - | 1 | 25 | 6.0 | 2 | 460k cr |
| Carrack | Heavy freighter | 60,000 | - | - | 4 | 120 | 11.0 | 3 | 7.99M cr |
| Lancer | Escort frigate | 200 | - | - | 12 | 40 | 3.5 | 3 | 557k cr |
| Warden | Patrol destroyer | 400 | - | - | 40 | 110 | 5.0 | 4 | 2.64M cr |
| Wisp | Survey frigate | 400 | - | - | 1 | 18 | 3.0 | 2 | 538k cr |

**Fittings.** Modules are fitted while the ship is docked, from the Ship Information window, and stack by multiplying. A fitted hull is worth its modules too, and loses them with it.

| Module | Effect | Built from |
| --- | --- | --- |
| Expanded Hold | cargo and ore hold +25% | 12 Hull Plating, 2 Thruster Assembly |
| Extraction Upgrade | mining yield +15% | 12 Extraction Head, 4 Power Core |
| Armour Plating | hull +35%, travel time +8% | 20 Hull Plating |
| Drive Tuner | travel time -12% | 12 Thruster Assembly, 4 Power Core |
| Weapon Battery | attack +35% | 16 Weapon Mount, 4 Power Core |
| Evasion Suite | contact chance -25%, escape +10 points | 12 Sensor Array, 6 Thruster Assembly |
| Survey Scanner | survey strength +0.8; sensors reach one jump further | 14 Sensor Array, 3 Power Core |

- **Route policies:** Shortest; Lowest risk (each lane costs more the higher its destination's rating); Avoid Frontier. Routes are A\* over the hex grid, so planning a 100-jump route across a 20,000-system chart takes about 1.4 ms.
- **Cargo insurance** is an option on Haul and Trade orders: each loaded trip buys cover priced on the worst rating along the route (0.2% of the cargo's value plus 0.04% per risk point), paying 80% of what is lost.
- **Ship Information** shows each hull's stats as built and as fitted, its slots, pilot, condition, insurance and service record (jumps, ore mined or cargo hauled, kills). Ships can be renamed.
- Fleets repair free when they dock; a ship's pilot sets its mining (Mining), travel and escape (Navigation) and damage (Gunnery).

## Industry

Industry turns ore into everything else in four steps: refine ore into minerals, build components from minerals, then build modules, hulls or an Outpost Kit from components. Every step is a job at a station, using what is in your hangar there.

- **Refining** works in batches of 100 units of ore. Yield is 55% in Charter space, 62% in Contract space and 70% at outposts, plus up to 10 points from an Industry Director and 5 from Charter standing (cap 85%). The fee is 2% of the minerals' value, or the owner's refinery tax at an outpost.
- **Manufacturing** needs a blueprint original (unlimited runs) or one copy per run. The install fee is 1% of the materials' value; Industrial stations build 15% faster.
- **Research** belongs to the corporation: material efficiency (ME) 0 to 10 saves 1% of every material per level, time efficiency (TE) 0 to 10 saves 2% of build time per level. Each level takes longer than the last (2x the run time x the level, at least an hour); Research stations work 30% faster. The fee is 2% of the original's price per level.
- **Copying** makes one-run copies of ship blueprints in half a run's time each. Copies trade at Charter hubs (about 1/40 of the original's price), so you can build in several places at once or sell copies to others.
- An original busy with research or copying cannot build at the same time.

| Blueprint | Original costs | Time per run | Components per run | Hull sells for |
| --- | --- | --- | --- | --- |
| Mattock | 1.8M cr | 4 h | 50 Hull Plating, 20 Thruster Assembly, 25 Extraction Head | 334k cr |
| Drover | 2.4M cr | 5 h | 90 Hull Plating, 40 Thruster Assembly, 5 Power Core | 460k cr |
| Lancer | 3.0M cr | 5 h | 70 Hull Plating, 40 Thruster Assembly, 40 Weapon Mount, 10 Sensor Array | 557k cr |
| Warden | 14M cr | 10 h | 300 Hull Plating, 120 Thruster Assembly, 60 Power Core, 220 Weapon Mount, 40 Sensor Array | 2.64M cr |
| Quarrier | 16M cr | 12 h | 350 Hull Plating, 120 Thruster Assembly, 80 Power Core, 200 Extraction Head | 2.69M cr |
| Carrack | 45M cr | 24 h | 1,500 Hull Plating, 450 Thruster Assembly, 300 Power Core, 40 Sensor Array | 7.99M cr |
| Outpost Kit | 60M cr | 48 h | 3,000 Hull Plating, 800 Power Core, 200 Sensor Array, plus 2,000 Nocturnite, 400 Aetherium, 1,000 Electronics | 48.9M cr |

Component blueprints cost 250k to 400k cr and make 10 units a run in 30 to 40 minutes; module blueprints cost 0.9M to 1.2M cr and make one module an hour. AI industrialists hold component and hull originals with some research already done, so the player competes on margin, not on access.

## Finance

Money is a tool as well as a score: borrow to grow faster, lend to earn on idle cash, and own pieces of your rivals.

**Borrowing from the Consolidated Bank.**

- Credit limit is half your net worth, at least 1M cr. Terms of 30, 60 or 90 days.
- Interest is quoted per 30 days: 1.5% for an A rating up to 4.5% for E. Principal and interest are taken in equal daily installments.
- A missed installment goes into arrears with a 1% penalty and drops your rating one grade. After 21 missed days the loan defaults: the bank seizes 30% of what is owed and your rating drops to E.

**Lending to other corporations.**

- AI corporations post loan requests on the Loan Board: amount, rate per 30 days, term and a credit rating from A to E.
- Fund one and the credits leave your wallet at once; daily installments come back from their wallet.
- A borrower that misses 7 days in a row defaults: you recover 30% and your standing with them drops. Repaid loans raise it.
- Pirate and ganker corporations borrow at the highest rates and default the most. That is the trade.

**Corporate shares (the NebulaOne Exchange).**

- Every AI corporation has 1,000,000 shares listed. Price tracks book value per share, scaled by sentiment from its 30-day profit trend, with daily noise.
- Buying or selling more than 1% of shares in a day moves the price against you.
- Dividends: every 30 days each corporation pays out 20% of its net-worth growth since the last dividend, capped at a quarter of its cash.

**Your own listing.** Once you are worth 50M cr and have 30 days of accounts, the Boardroom can float 10% to 49% of your 10 million shares. The underwriters value you at 1.2x net worth and keep 5%; you keep control.

- **The board** reviews you every 30 days: growth in net asset value per share against a target that starts at 8% and moves with your record (4% to 15%). Confidence moves by 250 x (growth - target) points, between -30 and +20 a review.
- **Dividends** raise confidence by up to 15 points; only the float is paid (your own holding is the company's). **Buybacks** at a 5% premium raise it by 3; buying back the whole float takes you private again.
- Below 10 confidence the board acts: it forces a payout of 5% of cash and dismisses your CFO, then resets to 30.
- **Takeovers.** Buy more than half of an AI corporation's shares on the Exchange, then buy out the minority at the share price. Everything it owns becomes yours: cash, hangars, ships and fleets (which then need your pilots), outposts, loans in both directions and its research levels.

## People

Ships are useless without pilots, and a corporation scales through its executives. Staff cost daily wages, so every hire has to pay for itself.

- **Recruitment.** Each Trade Hub has a candidate pool that refreshes every 7 game days. Hiring costs 3 days of wages up front.
- **Pilots** have three skills from 1 to 100: Mining, Navigation and Gunnery. Wages run 2,000 to 8,000 cr a day by skill.
- **Executives** fill one seat each and earn 15,000 to 40,000 cr a day.

| Seat or skill | Effect at 100 |
| --- | --- |
| Pilot Mining | Yield ×1.2 (×0.8 at 0) |
| Pilot Navigation | Jump time −10% (+10% at 0) |
| Pilot Gunnery | Combat strength ×1.2 (×0.8 at 0) |
| Chief Financial Officer | Broker fee and sales tax −30% |
| Industry Director | Refining yield +10 points, job time −20% |
| Security Chief | Encounter chance −30%, combat strength +10% |
| Logistics Director | Fleet travel time −10% |

- **Losses.** When a ship dies its pilot escapes in a pod with a chance set by Navigation (60% to 90%). Survivors are injured for 3 days; the rest are gone.
- **Traits** (one per person): Steady, Reckless, Frugal, Ambitious. They nudge skills, wages and survival. Morale and loyalty come later.

## Rival corporations

The chart size sets how many AI corporations share it: 12 on Compact, 40 on Standard, 120 on Vast, 300 on a Galaxy and 600 on an Expanse. The mix is fixed: 25% traders, 20% miners, 15% industrialists, 15% pirates, 7% gankers and the rest mercenaries. Names and tickers are generated per archetype (Mercantile and Freight for traders, Syndicate and Corsairs for pirates, and so on), homes are spread over suitable stations, and on big charts a few start with outposts in the Frontier. Each one thinks about once an hour and only looks a few jumps around its fleets, never at the whole galaxy.

| Archetype | Share of rivals | Lives in | Behaviour | What it means for you |
| --- | --- | --- | --- | --- |
| Trader | 25% | Charter and Contract space | Scans station prices, runs the most profitable trade routes it can afford | Competes for your arbitrage; closes price gaps |
| Miner | 20% | All three zones | Mines the best ore it can defend, refines it and sells minerals | Sets mineral prices; a target for pirates |
| Industrialist | 15% | Charter and Contract space | Buys minerals and components, builds modules and hulls, sells them below shipyard prices | Main buyer of your minerals; ship-price competition |
| Pirate | 15% | Contract space and the Frontier | Roams with combat fleets and attacks haulers and miners | The reason Contract space pays more |
| Ganker | 7% | Charter space | Suicide-attacks rich, unescorted haulers before the Wardens arrive | Never haul a fortune alone |
| Mercenary (PvP) | 18% | Contract space and the Frontier | Patrols to hunt pirates; collects bounties | Their patrols pull Contract-space risk ratings down |

- **Growth.** AI corporations reinvest profit into ships, hire pilots and take bank loans. A corporation that runs out of money and ships is restructured: new investors inject capital and its share price halves.
- **Relations.** Standings move with what you do to and for them: trade, attacks, funded loans, defaults.
- **Visibility.** Their fleets appear on the hex chart. The Corporations window shows each one's net worth, share price, fleet size and standings.

## Law, risk and piracy

Who keeps the law depends on the zone; everything the law does not cover, the underwriters put a number on.

| Zone | Police | Base risk | Refinery yield | Price premium | Stations |
| --- | --- | --- | --- | --- | --- |
| Charter | Charter Wardens arrive in round 1 + risk/20 and destroy every aggressor; attackers lose 2.5 Charter standing | 8 + 3 x depth | 55% | 0%, plus a 1% Charter levy on sales | 1 to 3 per system: hubs, factories, Agri Domes, research |
| Contract | None. Aggression still costs 0.5 Charter standing | 35 + 5 x depth | 62% | 12% | 0 to 2: mining colonies, garrisons |
| Frontier | None | 70 | 70% at outposts | 30% | Only outposts |

**Risk ratings.** Every system carries an underwriters' grade, AAA to D, from a 0 to 100 risk figure. It is worked out daily and published weekly (with news of upgrades and downgrades); the gap between today's figure and the published one is the outlook arrow shown beside every grade.

```latex
risk = \mathrm{clamp}\left(base + 12\log_2(1 + heat) + 25\log_{10}(1 + losses) + 3\log_2\left(1 + \frac{hostile\ hours}{24}\right) - 8\,[outpost\ online],\ 0,\ 100\right)
```

Heat is battles and kills (decays 15% a day), losses are millions of credits destroyed there (decays 10% a day), hostile hours are raiding, besieging or outlaw fleet-hours (decays 20% a day). The result is blended 70% own and 30% from neighbouring systems. Grades: AAA below 10, AA below 20, A below 30, BBB below 42, BB below 55, B below 68, CCC below 82, D above.

- **Hull insurance** costs 3% + 0.2% x risk of the hull and fittings' value per 30 days, priced on the riskiest system where the ship works, and pays 70% on loss.
- **Cargo insurance** is bought per trip on Haul and Trade orders (0.2% + 0.04% x the worst risk on the route) and pays 80% of cargo lost.
- **Encounters.** Raiding and patrolling fleets scan every 10 minutes. Contact needs the attacker strong enough for the guard it sees; the target's Navigation and its Evasion Suites cut the chance. Non-combat defenders try to break off each round.
- **Pirates** raid outside Charter space and sometimes besiege outposts; **gankers** suicide-attack rich, unescorted haulers in Charter and inner Contract space before the Wardens arrive; **mercenaries** patrol where outlaws are and take bounty contracts.
- **Bounties.** The Charter Authority pays 25% of a ship's value to whoever destroys an outlaw's ship, and posts bounty contracts on outlaw corporations working near you.

**Piracy as a player.** Put combat ships on a Raid order and they attack convoys passing through. Loot goes into your holds. In Charter space the Wardens will come for you and your Charter standing falls; in Contract space only your reputation suffers; in the Frontier nobody is watching, but your victims remember.

## Territory

The Frontier belongs to whoever can anchor an outpost there and keep it. An online outpost gives its owner the system: a market, a 70% refinery, factories and hangars where the richest ore is, and a tax on everyone who uses them.

1. **Get a kit.** Buy an Outpost Kit at a Contract-space trade hub or build one from its blueprint (48.9M cr of materials, 48 hours). It is 40,000 m3: only a Carrack's hold takes it.
2. **Anchor.** Fly it into a Frontier system nobody holds, out of dock, and press Anchor outpost. Anchoring takes 24 hours, with the shield at a quarter.
3. **Fuel.** Once online it burns 250 Fuel Cells a day from its owner's hangar there. Without fuel it goes offline: no market, refinery, taxes or shield regeneration.
4. **Tax.** The owner sets a market tax and a refinery tax, 0 to 25% each, paid by everyone else who sells or refines there. The owner pays none.
5. **Defend.** A Siege order wears the shield down (20,000 points; fleet strength x 30 an hour). At zero the outpost is reinforced for 24 hours and cannot be hurt: the owner's window to bring patrols. After that the hull (30,000 points) takes the damage; at zero the outpost, every hangar in it and every hull docked there are destroyed.

An online outpost knocks 8 points off its system's risk. AI owners send their warships to patrol a besieged outpost; strong pirate bands sometimes besiege outposts near their home. The Territory window lists your outposts with shield, hull, fuel days and tax collected, and every outpost on the chart with a Besiege button.

## Contracts

The Contract Board turns other people's needs into work: stations post supply and courier jobs, the Charter Authority posts bounties, and miners pay for patrols. New work appears daily, mostly within 10 jumps of your home so the board stays relevant on the largest charts, and open jobs expire after 3 days.

| Kind | Posted by | You do | Paid |
| --- | --- | --- | --- |
| Supply | A station short of goods, minerals or components | Deliver the quantity to that station within 6 days | 1.2 to 1.4x the station's price, on delivery |
| Courier | A station's consignment, or a corporation | Carry sealed goods from A to B within 5 days; you hold collateral equal to their value | A fee scaled by jumps and the worst risk on the route, plus the collateral back |
| Bounty | The Charter Authority, or a corporation | Destroy a number of an outlaw corporation's ships within 10 days | On the last kill |
| Patrol | A miner, or a station's traders' guild | Keep warships on a Patrol order in a risky system for 12 to 36 hours within 5 days | On the last hour; the system's heat drops |

You can post work too: courier your own goods, buy goods delivered, put a bounty on a rival, or pay for a patrol, with the reward held in escrow. AI traders take courier work and mercenaries take bounties, from the board and from you. A courier job you fail keeps the goods and loses the collateral; failing anyone costs standing with them.

## Fog of war and intel

With fog of war on (the default, and a new-game option) you only see what your sensors see. Live order books and foreign fleets show only where you have eyes. Everywhere else the Market window shows the last prices you recorded, stamped with their age, and you can place limit orders there but not trade instantly.

- **Sensors.** Your headquarters sees its own system and one jump around it. A fleet sees its own system; with a survey array of 0.8 or more (a Wisp, or a Survey Scanner) it sees one jump around, with 1.6 two jumps, and on a Scout order one jump further again (up to three). Online outposts see two jumps. Allies share everything their fleets and outposts see.
- **The Charter Exchange** publishes every Charter hub's prices at each day's close, so the core markets are never more than a day out of date.
- **Intel brokers** sell a region's market report (50k cr plus 30k per station: a snapshot of every book there, as of now) or a day of fleet sightings (150k cr plus 8k per system: every fleet in the region shows on the chart for 24 hours).
- **The chart's Intel view** shades live sight, broker sightings and your sites, so you can see where you are blind.
- AI corporations are not fogged: each plans from its own neighbourhood. In multiplayer every corporation would carry its own picture of the galaxy.

## Exploration

Every system hides a few sites each fortnight, worked out from the seed so nothing is stored until someone finds one. A Charter system holds one about a quarter of the time, a Contract system about half the time, a Frontier system one to three.

| Site | What it is | Found in | What it gives |
| --- | --- | --- | --- |
| Derelict | A wrecked hull, from a Mattock in Charter space to a Carrack or Warden in the deep Frontier | Everywhere | Components and fuel, often a module, sometimes a blueprint copy |
| Relic cache | Pre-Charter relays, survey beacons, research hulks, sealed vaults | Contract space and the Frontier | Rare minerals and Electronics; a 45% chance of research data worth a free ME or TE level |
| Ore pocket | A rich hidden belt of deep ore | Everywhere | A belt only the finder can mine, for 20 days |
| Drift lane | A short-lived shortcut to a system 10 to 40 hexes away | Contract space and the Frontier | A lane only the finder can use, for 3 to 6 days |

- **Surveying.** A Survey order scans the system in 480 / (0.4 + survey strength) minutes: about 6 hours for a Wisp, under 4 with a Survey Scanner fitted, half a day or more for other hulls. Each hidden site is found with a chance of 20% plus 30% per point of survey strength, less for deeper sites (8% to 90%); a second survey ship adds 15% of its strength.
- **Salvage.** Derelicts and relic caches stay open for 12 days after they are found. A Salvage order works the site for 3 hours (5 for a relic cache) and carries the haul to a station: about 100k cr of value at a Charter wreck, up to about 3M cr at the deepest Frontier sites.
- **Rivals survey too.** Miners, traders and industrialists run their own surveys; sites are first come, first served, and the wire reports big finds near you.
- **The Wisp** (538k cr; the blueprint original costs 3.2M cr) is the fastest hull in the registry, built around a survey array of 1.0.

## Wars and diplomacy

Outside the Frontier, fighting is a crime until you buy the right to it. The Charter Authority sells war licences, and corporations sign pacts with each other.

| Relation | How it starts | What it does | How it ends |
| --- | --- | --- | --- |
| War | A Charter war licence: 5M cr or 1.5% of the target's net worth, whichever is more; it takes effect after 24 hours | Fighting between the two is lawful everywhere for 14 days: no Wardens, no Charter standing lost. Each side's war score is the value it destroys | The licence runs out, or a peace, often with reparations |
| Non-aggression pact | An offer the other side accepts (an AI corporation needs your standing at 0 or better and signs at most two) | Neither side's raiders or patrols attack the other's fleets | 24 hours after either side gives notice |
| Alliance | An offer the other side accepts (an AI corporation wants standing 6, or 1 with a common enemy, and takes at most three allies) | Shared sensors; allied patrols join battles; allies are called to arms when one of you is attacked | 24 hours after notice |

- **Peace.** The losing side pays. An AI corporation that is clearly beaten (your score above twice its own plus 3M cr) will pay up to 30% of the score gap, capped at 15% of its cash. One that is winning wants a quarter of the gap, and in the first two days of a war it signs only for well over that.
- **Mercenaries.** Hire a mercenary corporation for 3 to 30 days at 150k cr a day plus 12k for each point of its fleet strength. It takes out a licence against your target (effective in 6 hours) and sends its fleets after the target's outposts and home system. The target holds it against you (standing -3).
- **AI corporations** retaliate against aggressors (declaring war, or hiring mercenaries when too weak), fight over Frontier sovereignty and old grudges, hire mercenaries against commercial rivals, settle wars that go badly, and sign pacts and alliances among themselves. Pirates and gankers sign nothing and need no licence against them: they are fair game already.
- **The Diplomacy window** (DIPL) shows your wars with their scores, your pacts and the offers waiting for you, lets you make a move against any corporation, and lists the galaxy's wars and alliances.

## The living galaxy

New events start every day: about 0.8 plus one per 250 stations a day, at most 4 plus one per 60 stations running at once, most of them within 12 jumps of your headquarters. Many are announced on the wire before they start, so reading it pays: buy before the strike, sell into the outbreak.

| Event | Where | Effect | Lasts |
| --- | --- | --- | --- |
| Shortage | A station | Demand for one good, component or mineral jumps 2.5 to 4x | 3 to 7 days |
| Glut | A station | A flood of one good: stock up to 4x what the station wants, demand quartered | 3 to 6 days |
| Strike | An Industrial, Agricultural or Mining Colony station; announced 1 to 2 days ahead | Production stops | 3 to 6 days |
| Boom | A region | Luxury Goods and Electronics demand 1.6 to 2.2x, Fuel Cells somewhat up | 4 to 9 days |
| Outbreak | A region | Medical Kits demand 2.5 to 3.5x, Ration Packs 1.4x | 4 to 9 days |
| Levy edict | All Charter space; announced 2 days ahead | The Charter levy on sales rises to 3%, or is suspended | 7 to 14 days |
| Embargo | All Charter space; announced 2 days ahead | One of Luxury Goods, Electronics, Nocturnite or Zyrium cannot be traded in Charter space; smugglers elsewhere pay 1.6x | 7 to 12 days |
| Ion storm | A system; forecast 6 to 12 hours ahead | All its lanes close | 12 to 40 hours |
| Raider surge | A region | Pirate bands are drawn in, nearly twice as likely to raid there | 4 to 9 days |
| Gold rush | A deep Contract or Frontier system | A temporary belt of 250k to 960k units of rich ore | 6 to 12 days |
| Crackdown | A Charter region | Wardens answer in the first round and Charter bounties double | 5 to 10 days |

The News Wire's Events tab lists what is running and what is coming; the chart's Events view marks each one where it happens.

## Divisions and directors

From 0.4 a corporation can run itself: executives in the seats run six divisions inside the limits you set, so a large company is managed rather than micromanaged. Hand a division its fleets, tick *Let the director run this division*, and the director gives the orders, hires pilots for empty hulls and books every credit to the division's own profit and loss. A manual order to one of its fleets hands that fleet back to you.

| Division | Seat | What the director does | Limits that matter |
| --- | --- | --- | --- |
| Mining | Mining Director | Picks the best belts in range, refines the ore at the base, sells the minerals or keeps them for Industry | Base, radius, zones, riskiest system |
| Trade | Trade Director | Finds trade runs in range, sells leftover cargo later rather than dumping it, insures cargo | Capital in cargo at once, zones, risk |
| Haulage | Haulage Director | Fuels outposts, consolidates scattered stock to the base, carries materials to your projects, keeps colonies supplied, runs couriers | Monthly budget, what to carry |
| Industry | Industry Director | Keeps manufacturing lines busy, buys missing inputs, researches when a line is free, never sells its own inputs | Lines, product, monthly budget |
| Security | Security Director | Escorts the other divisions, patrols where they work, takes bounties; holds back when the threat is over 0.75 of its strength | Zones, risk, escort and guard duties |
| Intelligence | Intelligence Director | Counter-intelligence on watch, and runs the operations you order | Monthly budget |

The Chief Financial Officer is the seventh seat: it runs no division but cuts broker fees and sales tax by up to 30%. A director checks the whole route of every order against the zones and risk limits, using the worse of a system's rating and its live risk, and avoids systems with recent violence. A director who suits the seat by trait gets a bonus.

**Monthly reports.** Every 30 days each division files revenue, costs, result and a verdict, kept on its tab. Mining is valued at what its ore refines to; Trade and Industry book the value they add, not their turnover.

**Staff.** Directors have loyalty and ambition. They ask for raises, and good ones get poached: a rival offers 25 to 60% more and you match it or let them go. Unanswered requests cost loyalty; a director with none left hands in notice. The STAFF alert in the top bar counts open requests.

**Espionage.** The Intelligence Director runs operations against a rival. The chance of success is 0.25 + the operation's base + half your director's skill − 0.45 × the target's counter-intelligence, between 5% and 92%. A failure may expose the agents: standing −4 with the target and −0.5 with the Charter.

| Operation | Cost | Days | Base | On success |
| --- | --- | --- | --- | --- |
| Dossier | 250k cr + 0.05% of their net worth | 2 | +0.45 | Their fleets, books and finances, live for 10 days |
| Research theft | 1.5M cr | 6 | +0.05 | Copies a blueprint's research levels, up to 2 levels at a time |
| Sabotage | 2M cr | 4 | 0 | Delays every manufacturing and research job by 3 days |
| Smear campaign | 3M cr + 0.1% of their net worth | 5 | +0.10 | Their share price falls and stays depressed (drag up to 40%, fading) |

## Construction and megaprojects

Building things changes the map for everyone: an outpost grows module by module, and a megaproject funded by several corporations adds a lane, extends the Wardens' reach or makes a new hub. Every project lists materials and credits. Materials are delivered physically to its yard, a station within 3 jumps of the site, which gives haulers work; credits can come from anyone, and whoever pays in holds a share of the result.

**Outpost modules.** Six modules, three tiers each. Tier t needs 1, 2 or 4 times the base materials and 4M × t² cr, and builds in 2t days.

| Module | Per tier |
| --- | --- |
| Refinery | +4 points refining yield, 15% faster refining |
| Factory | 10% faster manufacturing |
| Market Exchange | +50% stock and trade; tier 2 draws job seekers, and allows a Trade Nexus |
| Laboratory | 20% faster research and copying |
| Defence Grid | +60% shield and hull; its guns hit besiegers |
| Fuel Silo | 20% less fuel |

**Megaprojects.**

| Project | Credits | Days | Where | Effect |
| --- | --- | --- | --- | --- |
| Jump Gate | 250M cr | 20 | Two systems 8 to 40 hexes apart, each with a station within 3 jumps | A 2-hex lane between them; routes and the AI use it at once. Others pay a 20k cr toll per ship, split among the backers by share |
| Charter Beacon | 120M cr, 40% paid by the Charter | 12 | A Contract system | Base risk −15 there and −8 next door; the Wardens answer in the beacon's system in 2 rounds plus 1 per 20 risk |
| Trade Nexus | 150M cr | 15 | Your outpost with a tier-2 Market Exchange | It becomes a hub: twice the stock and trade, and staff come there looking for work |
| Deep Survey Array | 90M cr | 10 | Any system | Live sensors 3 jumps around it, for the founder |

AI corporations pay into projects near their interests, so a gate between two busy hubs fills quickly. A project still unfunded after 120 days winds up and refunds 80% of what was paid in. Haulage directors carry materials to your yards on their own.

## Planets and colonies

Every system has 3 to 11 planets, generated from the system's seed whenever they are asked for and never stored, so a 20,000-system chart costs no memory for them. Each has a type, a size, a habitability and a richness. Charter planets are settled already; in Contract space and the Frontier a corporation can found a colony.

**Settling.** Buy a Colony Kit at a Charter hub (or build one: hull plating, power cores, sensor arrays, rations and medical kits), carry it in a Carrack (20,000 m³) to the planet and settle it from the Colonies window. The colony starts with 5,000 people and a spaceport market.

| Planet | What its people make | Grows |
| --- | --- | --- |
| Garden | Ration packs, luxury goods, medical kits | Big |
| Ocean | Ration packs, medical kits, fuel cells | Big |
| Rocky | Ferrox, Silvane, Coriel | Small |
| Volcanic | Vantium, Lucenite, Zyrium | Small |
| Barren | Silvane, Vantium, Aetherium | Small |
| Ice | Fuel cells, Coriel, Silvane | Small |
| Gas giant | Fuel cells, Nocturnite | Small |

**Needs and growth.** Per thousand people a colony eats 25 ration packs, 5 fuel cells, 2 medical kits, 1.2 electronics and 0.5 luxury goods a day. *Needs met* weighs the stock against that (rations count 45%). Above 50% the colony grows, by up to 3% a day, towards its room (3,000 + 40,000 × size × habitability × (1 + habitat tier) people); below it, people leave. Under 30% for 7 days brings protests, and at 14 days riots stop taxes and output until supplies return.

**What it pays.** Taxes of 5 cr per person a day at full supply, and a share of what the planet makes delivered to your hangar at the spaceport. Its market sells the rest to anyone, and buys supplies, so traders bring food when prices rise; a Haulage division keeps your colonies stocked.

| Upgrade (3 tiers, 5M × t² cr, 3t days) | Per tier |
| --- | --- |
| Habitats | Room for 2, 3, then 4 times the people |
| Extractors | +50% of the planet's output to you |
| Spaceport | +50% market depth; sensors one jump out |

## Capital markets

The Finance window adds four instruments in 0.4, each a different bet on the same economy: futures on hub prices, bonds, short sales of rivals' shares, and an insurance book of your own. All of them are cash flows through the ledger (*Futures and shorts*, *Margin*, *Bonds*, *Bond coupons and fees*, *Underwriting*), and open positions count in net worth at their mark.

| Instrument | How it works | Costs and limits |
| --- | --- | --- |
| Futures | Long or short a mineral, trade good or component at a Charter hub for 7, 14 or 30 days; settled in cash against the hub's price at expiry. The forward price blends today's spot with the hub's 14-day average (weight e^(−days/5) on spot) plus 0.15% a day of carry | 15% margin, called at 8%, closed out if you cannot pay; 0.5% spread and 0.2% fee; one contract at most half your net worth; 24 open |
| Bonds (issue) | 1M cr units for 30, 60, 90 or 180 days; a coupon every 30 days and the whole principal at maturity. Coupon per 30 days 1.0% (A) to 2.6% (D), +0.04% per month of term; cheaper than the bank and nothing to repay until the end | 1% fee; outstanding bonds up to 30% of net worth (40% if listed); none at an E rating; call early at 102; six days overdue is a default (rating E, board −25) |
| Bonds (buy) | Rivals put issues on offer for 6 days; you collect their coupons and principal | Sell back at 98.5% less 2% per rating step; a default repays at most 40% |
| Short selling | Borrow a rival's shares from the Exchange and sell them; buy back later. Selling moves the price down, covering moves it up | Up to 30% of the public float can be lent; 50% margin, called at 25%; 0.5% fee, then 0.05% a day; you owe its dividends; a takeover closes the short at the bid price, a bankruptcy pays it in full |

A squeeze is allowed: a corporation long a hub's futures can buy that hub dry before expiry, and the self-test does exactly that. It costs what the stock costs, and the hub refills within hours when traders arrive, so the timing has to be right.

**Takeover defences.** Both sides of a takeover now have moves.

| Defence | Used by | When | Effect |
| --- | --- | --- | --- |
| Poison pill | AI corporations (adopted once you hold 15%); you, once listed (2M cr legal fees, board −4) | A raider passes 25% | New shares equal to all the shares out go to every holder but the raider: its stake halves, the price halves. Fires once |
| White knight | AI corporations, once you pass 30%; you, against a raider (a friend at standing 1.0 or better) | Majority in danger | A friendly corporation buys the rest of the float, so the raider cannot reach a majority on the market. Costs you 1.5 standing with the friend |
| Greenmail | You, against a raider | A raider holds 10% or more and gives you 30 days | Buy its stake back at 125% of the price; the shares are cancelled; board −6 |

A listed corporation draws raiders: about once in 80 days a rival with the cash starts buying 1 to 2% of your shares a day towards a stake of 16 to 28%. From 10% it lobbies your board (confidence −0.35 a day) and demands greenmail. Ignore it and at the deadline the board takes −10 while the raider dumps its stake and leaves. The RAID alert in the top bar opens the Boardroom.

**The underwriting desk.** Buy a 5M cr Charter licence and write hull cover for other corporations. You set the price against the Charter underwriters' rate (3% of hull value plus 0.2% per risk point, per 30 days), the riskiest work you will cover, and the most cover in force: up to four times your cash, and nothing new while your cash is under a tenth of the cover in force. AI ships buy 30 days of cover more often the cheaper it is, and a loss on your book costs you 70% of the hull.

| Price, risk limit | Policies written | Premiums | Claims | Loss ratio |
| --- | --- | --- | --- | --- |
| 1.0x, risk 60 | 387 | 51.3M cr | 36.7M cr | 72% |
| 1.0x, risk 100 | 489 | 73.1M cr | 58.9M cr | 81% |
| 0.7x, risk 100 | 608 | 60.1M cr | 98.2M cr | 163% |
| 1.4x, risk 30 | 125 | 13.4M cr | 17.9M cr | 134% |

Simulated over 120 days on a Standard chart. The desk is a real risk business: priced at the Charter rate it earns, priced cheap it loses, and a thin book is at the mercy of one bad week.

## UI and presentation

NebulaOne looks like a corporation's operations room: smoked-glass panels floating over a deep-space chart, condensed type, and one accent colour, your corporation's livery. Version 0.3 keeps the function keys, command line and price tape from 0.2 and drops its solid amber-on-black panels.

- **Header.** Your ticker and company name in your livery, cash, net worth with its day change, share price and board confidence once listed, war, offer, combat and siege alerts, the game clock and five speeds, over a thin livery stripe.
- **Function strip.** Every window has a mnemonic key (CORP, LEDG, MKT, ASST, FLT, SHIP, IND, SYS, JOBS, TERR, STAF, FIN, DIR, BRD, DIPL, INTL, WIRE, LOG, HELP), lit in your livery while open.
- **GO> command line** (press / or \`): type a window code (MKT FERROX opens the Ferrox market), a system name, a hex id, a station or an item, and the view jumps there.
- **The tape.** A scrolling ticker of prices at your home market with day changes in green and red, followed by the latest headlines; WIRE opens the full news wire.
- **The chart.** A flat-topped hex grid over a procedural nebula and two parallax star layers. Each hex is washed in its zone's colour (slate blue Charter, ochre Contract, rust Frontier); lanes are ivory, zone-border lanes amber, hubs boxed, outposts triangles in the owner's colour, your fleets chevrons in your livery, battles red rings. Six views: Zones, Risk (every system by its grade), Sovereignty, Regions, Intel (what you can see) and Events. It culls to the screen and drops detail as you zoom out, so a 20,000-system Expanse still draws every frame.
- **Windows** are glass: 90% opaque warm-grey panels with a livery stripe on the title bar and the window's code at its right. Inside they are spreadsheets first: sortable, filterable tables with ledger rules, charts only where the shape matters. Drag a title bar onto another window to dock it, or past the game window's edge to detach it onto another monitor (Dear ImGui multi-viewports; X11 on Linux); detached windows carry the same backdrop, so the glass looks the same on every screen.
- **Liveries.** Pick one of eight colours when you found the company (Signal teal, Cobalt, Violet, Rose, Copper, Gold, Lime, Ivory) and change it any time in the Corporation window. It colours the interface, your fleets and your outposts.

| Colour | Means |
| --- | --- |
| Your livery (Signal teal by default) | Yours, selected, actionable |
| Warm white | Text and neutral fleets |
| Amber | Warnings and alerts only |
| Green / red | Gains and losses; risk grades AAA (green) to D (red) |
| Slate blue / ochre / rust | Charter / Contract / Frontier |

**Fonts and logo.** Saira Semi Condensed for interface text, Share Tech Mono for every figure, and Chakra Petch for window titles, big names and the wordmark (all SIL Open Font License). The logo keeps its monogram, an N whose right stroke rises into a 1, in ivory with a Signal teal orbit; the app icon sits on a deep-space tile so it reads on any desktop.

**Window layout.** Since 0.4 no window can get lost and docking never needs a precise drag. Every window carries a menu (the three dots by its title or tab, or a right-click on either):

- *Dock* to the left, right, top or bottom of the chart, or *Dock as a tab with* any other open window.
- *Float* (undock) a docked window; double-clicking a tab does the same, and double-clicking a floating title bar docks it back where it was (or in the right-hand column).
- *Pop out* to its own OS window for another monitor, and *pop in* again.
- *Reset this window* to its default place and size.

The LAYOUT key on the function strip resets every window to the default layout (also `GO> RESET`), docks everything, saves the current layout under a name and recalls saved ones (`GO> LAYOUT name`), and can limit docking to drags with Shift held. When the game window is too narrow for every key, the strip tightens its keys, shortens the command line and moves the last keys into a MORE menu.

## Tutorial and onboarding

A new game starts with a guided tutorial that teaches one money loop end to end, then gets out of the way. Each step waits for the player to actually do the thing, highlights the button to press, and can be skipped; the whole tutorial can be replayed from Help.

1. **Welcome.** The hex chart, the top bar, the function-key strip, the GO> command line, the game clock, and what fog of war hides.
2. **Your corporation.** Open the Corporation window: wallet, net worth, ships and staff.
3. **Your assets.** Open Assets and find the Mattock and the Drover in your home station's hangar.
4. **Read a market.** Open the Market for Cindrite at home, then compare a neighbouring station.
5. **Form a mining fleet.** Put the Mattock and a pilot in a fleet and give it a Mine order on a belt in your home system, unloading at home.
6. **Let time run.** Unpause, speed up, and watch the fleet fly, mine and unload. Waits until ore reaches your hangar.
7. **Refine.** Turn the ore into Ferrox at the home station refinery.
8. **Sell.** Sell the Ferrox into a buy order, or post your own sell order and see the broker fee.
9. **Haul for profit.** Use the Drover on a Trade run: buy Ration Packs where they are cheap, sell where they are scarce.
10. **Read the danger.** The chart's Risk view, the underwriters' grades and how they price insurance, the pirate markers outside the Charter, and why the richest belts are furthest out.
11. **Money tools.** The Finance window: bank loans, the Loan Board and the Exchange.
12. **Free play.** Goals to aim for: first 10M cr net worth, first manufactured ship, first mining op outside the Charter, five contracts, an outpost in the Frontier, a listing on the Exchange, three hidden sites found, a salvaged derelict, reparations won in a war.

## Technical architecture

NebulaOne is C++20 with Dear ImGui (docking branch), ImPlot, GLFW and OpenGL 3.3, built with CMake on Windows and Linux. The key decision is a hard wall between the simulation and the screen: the simulation never includes a UI header, so the same code can later run on a server.

*[Diagram: architecture · Commands sit between every actor and the World]*

The UI and the AI never touch the World directly; both go through Commands, which is what lets a server replace the local loop later.

- **`nebula_core`** (static library): the World (universe, items, markets, corporations, fleets, industry jobs, loans, shares, staff, sight and intel, sites, treaties, events, divisions, projects, colonies, futures, bonds, shorts and the underwriting book), the tick loop, Commands, AI corporation brains and save/load. No graphics, no files except saves.
- **`NebulaOne`** (the game): platform layer (window, OpenGL, input, multi-viewports), the glass theme, deep-space backdrop and fonts, the hex chart, every window and the tutorial. It reads the World and changes it only by submitting Commands.
- **`nebula_selftest`** (tool): runs the simulation headless for N game days on any chart size and prints price, wealth and fleet statistics plus speed and memory; --features drives every rule through Commands (104 checks, any seed). Used to balance the economy, measure scale and catch regressions.
- **Commands.** Every action (buy, sell, place order, set fleet order, start job, take loan, hire) is a small struct with the issuing corporation's id. The player's UI and the AI brains both submit them; the World validates and applies each one between ticks, so the game still responds while paused.
- **Determinism.** One tick is one game minute. One seeded random generator per world. Money is integer cents (1 cr = 100), so ledgers never drift.
- **Saves.** A versioned binary file (format 4, `.n1save`) per slot, in `%APPDATA%\NebulaOne` on Windows and `~/.local/share/NebulaOne` on Linux, alongside the window layout.
- **Packaging.** CPack builds a Windows installer (NSIS) that puts NebulaOne on the desktop and in the Start menu, and a portable ZIP; plus a Linux tarball, .rpm and .deb whose install scripts put a launcher on the desktop of the user who installed them. For the portable builds, Settings has a button that makes the desktop shortcut. A GitHub Actions workflow builds both platforms on every push.

**Built to scale.** Nothing in the World grows with the square of the system count, so the same code runs an 80-system Compact chart and a 20,000-system Expanse.

- **Distances** come from breadth-first rows cached for the 384 most recently used systems; **routes** are A\* over the hex grid, where the straight hex distance is the heuristic.
- **Markets** post real orders only near activity and quote on demand elsewhere; station economies update hourly when active and every six hours otherwise; price history is kept only where corporations trade.
- **AI** decisions sample stations and systems within a few jumps of each fleet (about a dozen sources and 40 destinations per trade decision).
- **Indexes** per corporation (ships, fleets, the books holding its orders) and per system (fleets in space, rebuilt every 10 minutes) replace whole-world scans; people slots are reused and settled jobs, loans and contracts are compacted.

| Chart | Systems | Stations | Game minutes per real second | Memory | Save |
| --- | --- | --- | --- | --- | --- |
| Compact | 80 | about 75 | about 54,000 | 4 MB | 1.4 MB |
| Standard | 400 | about 390 | about 15,500 | 12 MB | 6 MB |
| Vast | 2,000 | about 1,900 | about 3,400 | 47 MB | 25 MB |
| Galaxy | 8,000 | about 7,800 | about 950 | 138 MB | 64 MB |
| Expanse | 20,000 | about 19,400 | about 400 | 284 MB | 130 MB |

Measured headless on one core of a Linux build; the fastest game speed needs 360 game minutes a second. Every size passes the save, reload and replay check.

**Multiplayer path.** A future `nebula_server` links the same core and owns the authoritative World. Clients send Commands over the network and receive state snapshots and deltas; AI corporations keep running on the server. Because all change already flows through Commands, the single-player game is the multiplayer game with the server in-process. One process already runs a Galaxy-sized universe faster than real time; beyond that, the generated regions are the natural seams to split the universe across region servers, with fleets handed over at border lanes.

## Roadmap

Version 0.1 was M1, the playable slice. Version 0.2 delivered what M2 to M4 used to promise (territory, deep industry, the corporate game, contracts) with the new law, the hex chart and procedural charts up to 20,000 systems. Version 0.3 was M3: fog of war and intel, exploration, licensed wars, pacts, alliances and mercenaries, and galaxy events, with the glass look and desktop shortcuts. Version 0.4 deepens the corporate game before the server work: divisions and directors, construction and megaprojects, planets and colonies, and capital markets. M4, a server prototype running the core headless, is next.

*[Diagram: roadmap · milestones M1 to M6, with v0.4 between M3 and M4]*

The remaining gate protects what everything after it depends on: a simulation that changes only through Commands, deterministic and replayable from a save, before a server takes over. Every system through 0.4 already meets it, and the selftest replays each chart size from a save to prove it.

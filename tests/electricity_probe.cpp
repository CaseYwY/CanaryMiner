#include "electricity_fixture.h"
#include <iostream>
#include <sstream>
using namespace electricity_fixture;
int main() {
    std::string line;
    while (std::getline(std::cin,line)) {
        if (line.size() > 2048) return 2;
        std::istringstream input(line); std::string command;
        input >> command;
        if (command == "sample") {
            std::string watts; int64_t elapsed = 0; input >> watts >> elapsed;
            auto e = sampleHoldEnergy(evidence(),{{T,0,d(watts)},{T+elapsed,elapsed,d("0")}},T,T+elapsed,day);
            if (e.status != ElectricityStatus::Ok || !e.completeKwh) return 3;
            std::cout << e.knownKwh.numerator.text() << ' ' << display(*e.completeKwh,12) << '\n';
        } else if (command == "bill") {
            std::string mode, quantity, r1, r2, threshold, base, factor, floor;
            input >> mode >> quantity >> r1 >> r2 >> threshold >> base >> factor >> floor;
            auto p = plan(); p.revisions[0].baseRatePerKwh = d(base); p.subtotalMultiplier = d(factor);
            if (mode != "none") {
                p.tiers = mode == "progressive" ? TierMode::Progressive : TierMode::AllUnits;
                p.revisions[0].bands = {{d(threshold),d(r1)},{std::nullopt,d(r2)}};
            }
            if (floor != "none") p.minimumCycleSubtotal = d(floor);
            ExactDecimal watts; if (!d(quantity).multiply(d("1000"),&watts)) return 4;
            auto b = evaluateBill(p,opening(),load(watts.text()),T+hour,T);
            if (b.status != ElectricityStatus::Ok || !b.charge || !b.ending) return 5;
            std::cout << b.charge->numerator.text() << ' ' << display(*b.charge,12) << ' ' << b.ending->importedKwh.numerator.text() << '\n';
        } else return 6;
        if (!input) return 7;
    }
}

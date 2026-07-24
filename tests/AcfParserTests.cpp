#include "acf/AcfGeometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

const char* kFixture = R"ACF(I
1200 Version
ACF

PROPERTIES_BEGIN
P acf/_name Boeing 737-800NG
P acf/_ICAO B738
P acf/_cgY -2.05
P acf/_cgZ 60.22
P acf/_m_empty 91514.04
P acf/_m_max 174700
P _engn/count 16
P _engn/0/_type JET_2SPOOL
P _engn/0/_part_x -16.27
P _engn/0/_part_y -5.74
P _engn/0/_part_z 55.25
P _engn/0/_part_psi 3.5
P _engn/0/_part_the 2.0
P _engn/0/_exhaust_os_xyz/0 0
P _engn/0/_exhaust_os_xyz/1 0
P _engn/0/_exhaust_os_xyz/2 2.5
P _engn/0/_thrust_max_limit 26300
P _engn/1/_type JET_2SPOOL
P _engn/1/_part_x 16.27
P _engn/1/_part_y -5.74
P _engn/1/_part_z 55.25
P _engn/1/_part_psi -3.5
P _engn/1/_part_the 2.0
P _engn/1/_exhaust_os_xyz/0 0
P _engn/1/_exhaust_os_xyz/1 0
P _engn/1/_exhaust_os_xyz/2 2.5
P _engn/1/_thrust_max_limit 26300
P _wing/count 8
P _wing/0/_part_x 0
P _wing/0/_part_y -4.82
P _wing/0/_part_z 50.62
P _wing/0/_semilen_SEG 19.16
P _wing/0/_Croot 25.49
P _wing/0/_Ctip 13.58
P _wing/0/_sweep_design 27.5
P _wing/0/_dihed_design 10
P _wing/1/_part_x 0
P _wing/1/_part_y -4.82
P _wing/1/_part_z 50.62
P _wing/1/_semilen_SEG 19.16
P _wing/1/_Croot 25.49
P _wing/1/_Ctip 13.58
P _wing/1/_sweep_design 27.5
P _wing/1/_dihed_design 10
P _wing/2/_part_x -16.7323
P _wing/2/_part_y -1.8701
P _wing/2/_part_z 58.8255
P _wing/2/_semilen_SEG 19.9803
P _wing/2/_Croot 14.4357
P _wing/2/_Ctip 9.5801
P _wing/2/_sweep_design 25.4
P _wing/2/_dihed_design 9
P _wing/3/_part_x 16.7323
P _wing/3/_part_y -1.8701
P _wing/3/_part_z 58.8255
P _wing/3/_semilen_SEG 19.9803
P _wing/3/_Croot 14.4357
P _wing/3/_Ctip 9.5801
P _wing/3/_sweep_design 25.4
P _wing/3/_dihed_design 9
P _wing/4/_part_x -34.5472
P _wing/4/_part_y 0.9514
P _wing/4/_part_z 67.3885
P _wing/4/_semilen_SEG 21.6864
P _wing/4/_Croot 9.5801
P _wing/4/_Ctip 5.5774
P _wing/4/_sweep_design 25.6
P _wing/4/_dihed_design 12.7
P _wing/5/_part_x 34.5472
P _wing/5/_part_y 0.9514
P _wing/5/_part_z 67.3885
P _wing/5/_semilen_SEG 21.6864
P _wing/5/_Croot 9.5801
P _wing/5/_Ctip 5.5774
P _wing/5/_sweep_design 25.6
P _wing/5/_dihed_design 12.7
P _wing/6/_part_x -56.2336
P _wing/6/_part_y 7.3163
P _wing/6/_part_z 79.8556
P _wing/6/_semilen_SEG 8.1693
P _wing/6/_Croot 3.6089
P _wing/6/_Ctip 1.3123
P _wing/6/_sweep_design 30.8
P _wing/6/_dihed_design 77
P _wing/7/_part_x 56.2336
P _wing/7/_part_y 7.3163
P _wing/7/_part_z 79.8556
P _wing/7/_semilen_SEG 8.1693
P _wing/7/_Croot 3.6089
P _wing/7/_Ctip 1.3123
P _wing/7/_sweep_design 30.8
P _wing/7/_dihed_design 77
PROPERTIES_END
)ACF";

}  // namespace

int main() {
    using namespace ffatmo::acf;

    const ParseResult result = parseAcfText(kFixture, "sanitized_737_fixture.acf");
    require(result.ok, "sanitized ACF parses");
    require(result.profile.acfFormatVersion == 1200, "ACF 1200 detected");
    require(result.profile.aircraftName == "Boeing 737-800NG", "aircraft name extracted");
    require(result.profile.aircraftIcao == "B738", "ICAO extracted");
    require(result.profile.engines.size() == 2, "two active engines extracted");
    require(result.profile.mainWingSegments.size() == 8,
            "four mirrored main-wing pairs extracted");
    require(result.profile.hasLeftWingtip && result.profile.hasRightWingtip,
            "wingtip candidates derived");
    require(result.profile.engines[0].centreBodyM.x < 0.0 &&
            result.profile.engines[1].centreBodyM.x > 0.0,
            "engine lateral positions remain mirrored");
    require(result.profile.engines[0].exhaustOriginBodyM.z >
            result.profile.engines[0].centreBodyM.z,
            "local aft exhaust offset is composed");
    require(std::abs(result.profile.leftWingtipBodyM.x +
                     result.profile.rightWingtipBodyM.x) < 0.1,
            "derived wingtips remain mirrored");

    const ParseResult invalid = parseAcfText("I\n1100 Version\nACF\n", "old.acf");
    require(!invalid.ok, "unsupported ACF version rejected");

    std::cout << "FFAtmo ACF parser tests passed\n";
    return 0;
}

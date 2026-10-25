// Windows header check (MinGW cross-compile in CI): the framework-free headers must build for Windows.
#include "../common/Convolver.hpp"
#include "../common/CurveFit.hpp"
#include "../common/EqBand.hpp"
#include "../common/Fft.hpp"
#include "../common/Halfband.hpp"
#include "../common/Platform.hpp"
#include "../common/UndoHistory.hpp"
#include "../common/UserPresets.hpp"
int main() { epsilon::PresetStore s("Check", ".check"); epsilon::openURL(""); return (int)s.list().size(); }

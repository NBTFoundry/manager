// SPDX-License-Identifier: MPL-2.0
#include "nbt/model/theme.hpp"
#include "nbt/worker/phone_routes.hpp"
#include "nbt/worker/worker.hpp"

#include <stdio.h>
#include <stdlib.h>

namespace nbt {
namespace worker {

int phoneTheme(Worker &worker, core::Span<char> out) {
    const char *car = model::kCarMakePath;
#ifdef NBT_TESTING
    car = getenv("NBT_TEST_CAR_MAKE"); // unset: a car that reports none
#endif
    int choice = model::kThemeAuto;
    (void)model::themeSettingLoad(worker.root().legacy(), choice); // unreadable: automatic
    snprintf(out.data(), out.size(), "THEME\t%s\n",
             model::themeCode(model::themeEffective(choice, model::themeCarRead(car))));
    return 200;
}

} // namespace worker
} // namespace nbt

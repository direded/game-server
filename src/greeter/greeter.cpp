#include "greeter/greeter.h"

namespace game::greeter {

std::string get_greeting(std::string_view configured_message) {
    return std::string{configured_message};
}

} // namespace game::greeter

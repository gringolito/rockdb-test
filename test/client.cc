// Copyright © 2019 Filipe Utzig <filipeutzig@gmail.com>
//
// ----------------------------------------------------------------------------
// "THE BEER-WARE LICENSE" (Revision 42):
// <filipeutzig@gmail.com> wrote this file. As long as you retain this notice
// you can do whatever you want with this stuff. If we meet some day, and you
// think this stuff is worth it, you can buy me a beer in return. Filipe Utzig.
// ----------------------------------------------------------------------------

#include "mpsync/sync.h"
#include "stubs/middleware.h"

namespace mpsync {
namespace test {

class Client final {
   public:
    Client() : sync_(mpsync::stubs::Middleware())
    {
    }
    ~Client();

   private:
    Sync sync_;
};

}  // namespace test
}  // namespace mpsync
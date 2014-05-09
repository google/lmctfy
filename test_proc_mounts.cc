#include <iostream>
#include <string>

#include "production/omlet/util/proc_mounts.h"

using namespace std;

int main() {
    for (const ::production_omlet::ProcMountsData &mount :
             ::production_omlet::ProcMounts()) {
      cout << mount.mountpoint << endl;
    }
    return 0;
}

#include "core/Params.h"

namespace lvs {

const ParamInfo kParams[(int)P::Count] = {
#define X(group, id, name, label, lo, hi, def, unit) {#group, name, label, lo, hi, def, unit},
    LVS_PARAM_LIST(X)
#undef X
};

const int kParamCount = (int)P::Count;

const char* ParamName(P p) {
  const int i = (int)p;
  return (i >= 0 && i < kParamCount) ? kParams[i].id : "";
}

} // namespace lvs

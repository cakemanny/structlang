#ifndef __CANONICAL_H__
#define __CANONICAL_H__
// vim:ft=c:

#include "fragment.h"

sl_fragment_t* canonicalise_tree(
        const target_t* target, temp_state_t* temp_state, sl_fragment_t* fragments);


#endif /* __CANONICAL_H__ */

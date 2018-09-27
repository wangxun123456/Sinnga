#ifndef OMNICORE_VERSION_H
#define OMNICORE_VERSION_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#else

//
// Omni Core version information are also to be defined in configure.ac.
//
// During the configuration, this information are used for other places.
//

// Increase with every consensus affecting change
#define CLIENT_VERSION_MAJOR       0

// Increase with every non-consensus affecting feature
#define CLIENT_VERSION_MINOR       3

// Increase with every patch, which is not a feature or consensus affecting
#define CLIENT_VERSION_REVISION     1

// Non-public build number/revision (usually zero)
#define CLIENT_VERSION_BUILD       0

#endif // HAVE_CONFIG_H

#if !defined(WINDRES_PREPROC)

//
// *-res.rc includes this file, but it cannot cope with real c++ code.
// WINDRES_PREPROC is defined to indicate that its pre-processor is running.
// Anything other than a define should be guarded below:
//

#include <string>

//! Omni Core client version
static const int OMNICORE_VERSION =
                    +100000000000 * CLIENT_VERSION_MAJOR
                    +    10000000 * CLIENT_VERSION_MINOR
                    +        1000 * CLIENT_VERSION_REVISION
                    +           1 * CLIENT_VERSION_BUILD;

//! Returns formatted Omni Core version, e.g. "1.2.0"
const std::string OmniCoreVersion();

//! Returns formatted Bitcoin Core version, e.g. "0.10", "0.9.3"
const std::string BitcoinCoreVersion();


#endif // WINDRES_PREPROC

#endif // OMNICORE_VERSION_H

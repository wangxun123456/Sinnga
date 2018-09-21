#ifndef BITCOIN_WITNESS_H
#define BITCOIN_WITNESS_H

#include <string>
#include <vector>
#include <functional>
#include <fs.h>
#include <boost/thread.hpp>

void MintStart(boost::thread_group& threadGroup);

#endif // BITCOIN_WITNESS_H

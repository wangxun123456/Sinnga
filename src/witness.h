#ifndef BITCOIN_WITNESS_H
#define BITCOIN_WITNESS_H

#include <string>
#include <vector>
#include <functional>
#include <fs.h>
#include <boost/thread.hpp>
#include <uint256.h>

void MintStart(boost::thread_group& threadGroup);

//refdynamic_global_property_object
class CDynamicWitnessProperty
{
public:
    static const uint8_t space_id = 0;
    static const uint8_t type_id  = 0;

    uint32_t          headBlockNumber = 0;
    uint64_t          time;
    uint160           currentWitness;
    uint64_t          nextMaintenanceTime;
    uint64_t          lastBudgetTime;
    uint64_t          witnessBudget;
    uint32_t          accountsRegisteredThisInterval = 0;
    /**
       *  Every time a block is missed this increases by
       *  RECENTLY_MISSED_COUNT_INCREMENT,
       *  every time a block is found it decreases by
       *  RECENTLY_MISSED_COUNT_DECREMENT.  It is
       *  never less than 0.
       *
       *  If the recentlyMissedCount hits 2*UNDO_HISTORY then no new blocks may be pushed.
       */
    uint32_t          recentlyMissedCount = 0;

    /**
       * The current absolute slot number.  Equal to the total
       * number of slots since genesis.  Also equal to the total
       * number of missed slots plus head_block_number.
       */
    uint64_t          currentAbsoluteSlot = 0;

    /**
       * used to compute witness participation.
       */
    uint160 recentSlotsFilled;

    /**
       * dynamicFlags specifies chain state properties that can be
       * expressed in one bit.
       */
    uint32_t dynamicFlags = 0;

    uint32_t lastIrreversibleBlockNum = 0;

    enum DynamicFlagBits
    {
        /**
          * If maintenanceFlag is set, then the head block is a
          * maintenance block.  This means
          * GetTimeSlot(1) - HeadBlockTime() will have a gap
          * due to maintenance duration.
          */
        maintenanceFlag = 0x01
    };
};

#endif // BITCOIN_WITNESS_H

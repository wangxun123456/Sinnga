#include "omnicore/notifications.h"
#include "omnicore/version.h"

#include "util.h"
#include "test/test_bitcoin.h"
#include "tinyformat.h"

#include <boost/test/unit_test.hpp>

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

using namespace mastercore;

// Is only temporarily modified and restored after each test
//extern std::map<std::string, std::string> mapArgs;
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;

BOOST_FIXTURE_TEST_SUITE(omnicore_alert_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(alert_positive_authorization)
{
    // Confirm authorized sources for mainnet
    BOOST_CHECK(CheckAlertAuthorization("17xr7sbehYY4YSZX9yuJe6gK9rrdRrZx26"));  // Craig   <craig@omni.foundation>
    BOOST_CHECK(CheckAlertAuthorization("1883ZMsRJfzKNozUBJBTCxQ7EaiNioNDWz"));  // Zathras <zathras@omni.foundation>
    BOOST_CHECK(CheckAlertAuthorization("1HHv91gRxqBzQ3gydMob3LU8hqXcWoLfvd"));  // dexX7   <dexx@bitwatch.co>
    BOOST_CHECK(CheckAlertAuthorization("16oDZYCspsczfgKXVj3xyvsxH21NpEj94F"));  // Adam    <adam@omni.foundation>
}

BOOST_AUTO_TEST_CASE(alert_unauthorized_source)
{
    // Confirm unauthorized sources are not accepted
    BOOST_CHECK(!CheckAlertAuthorization("1JwSSubhmg6iPtRjtyqhUYYH7bZg3Lfy1T"));
}

BOOST_AUTO_TEST_CASE(alert_manual_sources)
{


	gArgs.SoftSetArg("-omnialertallowsender", "");
	gArgs.SoftSetArg("-omnialertignoresender", "");

    // Add 1JwSSu as allowed source for alerts
	gArgs.AppendArgs("-omnialertallowsender", "1JwSSubhmg6iPtRjtyqhUYYH7bZg3Lfy1T");
    BOOST_CHECK(CheckAlertAuthorization("1JwSSubhmg6iPtRjtyqhUYYH7bZg3Lfy1T"));

    // Then ignore some sources explicitly
	gArgs.AppendArgs("-omnialertignoresender", "1JwSSubhmg6iPtRjtyqhUYYH7bZg3Lfy1T");
	gArgs.AppendArgs("-omnialertignoresender", "16oDZYCspsczfgKXVj3xyvsxH21NpEj94F");
    BOOST_CHECK(CheckAlertAuthorization("1HHv91gRxqBzQ3gydMob3LU8hqXcWoLfvd")); // should still be authorized
    BOOST_CHECK(!CheckAlertAuthorization("1JwSSubhmg6iPtRjtyqhUYYH7bZg3Lfy1T"));
    BOOST_CHECK(!CheckAlertAuthorization("16oDZYCspsczfgKXVj3xyvsxH21NpEj94F"));

	gArgs.RemoveArgs("-omnialertallowsender");
	gArgs.RemoveArgs("-omnialertignoresender");
}

BOOST_AUTO_TEST_CASE(alert_authorize_any_source)
{

    gArgs.SoftSetArg("-omnialertallowsender", "");

    // Allow any source (e.g. for tests!)
	gArgs.AppendArgs("-omnialertallowsender", "any");
    BOOST_CHECK(CheckAlertAuthorization("1JwSSubhmg6iPtRjtyqhUYYH7bZg3Lfy1T"));
    BOOST_CHECK(CheckAlertAuthorization("137uFtQ5EgMsreg4FVvL3xuhjkYGToVPqs"));
    BOOST_CHECK(CheckAlertAuthorization("16oDZYCspsczfgKXVj3xyvsxH21NpEj94F"));

	gArgs.RemoveArgs("-omnialertallowsender");
}

BOOST_AUTO_TEST_SUITE_END()

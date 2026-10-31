/* Host regression for Konoha channel dispatch; hardware callbacks are mocked. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

struct wiphy { int mtx; };
struct net_device { int identity; };
struct cfg80211_chan_def { int channel; };
struct cfg80211_ops {
	int (*set_monitor_channel)(struct wiphy *, struct net_device *,
				   struct cfg80211_chan_def *);
};
struct cfg80211_registered_device {
	struct wiphy wiphy;
	struct cfg80211_ops *ops;
	int num_running_ifaces;
	int num_running_monitor_ifaces;
};
static int calls, status;
static struct wiphy *expected_wiphy;
static struct net_device *expected_dev;
static struct cfg80211_chan_def *expected_chandef;
#define lockdep_assert_held(p) ((void)(p))
#define trace_rdev_set_monitor_channel(w, d, c) ((void)0)
#define trace_rdev_return_int(w, r) ((void)0)

/* FUNCTIONS */

static int callback(struct wiphy *wiphy, struct net_device *dev,
		    struct cfg80211_chan_def *chandef)
{
	assert(wiphy == expected_wiphy);
	assert(dev == expected_dev);
	assert(chandef == expected_chandef);
	calls++;
	return status;
}

int main(void)
{
	struct cfg80211_ops ops = { .set_monitor_channel = callback };
	struct cfg80211_registered_device rdev = { .ops = &ops };
	struct net_device dev = { .identity = 7 };
	struct cfg80211_chan_def chandef = { .channel = 36 };
	const int outcomes[] = { 0, -EBUSY, -EINVAL, -EIO };
	int cases = 0;

	expected_wiphy = &rdev.wiphy;
	expected_chandef = &chandef;
	for (int present = 0; present < 2; present++) {
		expected_dev = present ? &dev : NULL;
		for (int running = 0; running <= 3; running++) {
			for (int monitors = 0; monitors <= running; monitors++) {
				rdev.num_running_ifaces = running;
				rdev.num_running_monitor_ifaces = monitors;
				ops.set_monitor_channel = NULL;
				calls = 0;
				assert(cfg80211_set_monitor_channel(&rdev, expected_dev,
							   &chandef) == -EOPNOTSUPP);
				assert(!calls);
				cases++;
				ops.set_monitor_channel = callback;
				for (unsigned int i = 0; i < sizeof(outcomes) / sizeof(outcomes[0]); i++) {
					status = outcomes[i];
					calls = 0;
					assert(cfg80211_set_monitor_channel(&rdev, expected_dev,
								   &chandef) == status);
					assert(calls == 1);
					cases++;
				}
			}
		}
	}
	printf("{\"dispatch_cases\":%d,\"nullable_device_forwarded\":true,"
	       "\"driver_errors_preserved\":true}\n", cases);
	return 0;
}

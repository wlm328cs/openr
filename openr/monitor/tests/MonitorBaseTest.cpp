// Copyright 2004-present Facebook. All Rights Reserved.

#include <openr/monitor/MonitorBase.h>
#include <glog/logging.h>
#include <gmock/gmock-generated-matchers.h>
#include <gmock/gmock-matchers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openr/config/Config.h>

using namespace std;
using namespace openr;
using namespace testing;

// MockClass for mocking the processEventLog() function
class MonitorMock : public MonitorBase {
 public:
  MonitorMock(
      std::shared_ptr<const Config> config,
      const std::string& category,
      messaging::ReplicateQueue<LogSample>& eventLogUpdatesQueue,
      const size_t maxLogEvents)
      : MonitorBase(config, category, eventLogUpdatesQueue, maxLogEvents) {}
  MOCK_METHOD1(processEventLog, void(LogSample const& eventLog));
};

class MonitorTestFixture : public ::testing::Test {
 public:
  void
  SetUp() override {
    // generate a config for testing
    openr::thrift::OpenrConfig config;
    config.node_name = "node1";
    config.domain = "domain1";

    monitor = make_unique<MonitorMock>(
        std::make_unique<openr::Config>(config),
        category,
        eventLogUpdatesQueue,
        5 /* maxLogEvents = 5 */);
    monitorThread = std::make_unique<std::thread>([this]() {
      LOG(INFO) << "monitor thread starting";
      monitor->run();
      LOG(INFO) << "monitor thread finishing";
    });
    monitor->waitUntilRunning();
  }

  void
  TearDown() override {
    eventLogUpdatesQueue.close();
    LOG(INFO) << "Stopping the monitor thread";
    monitor->stop();
    monitorThread->join();
    LOG(INFO) << "Monitor thread got stopped";
  }
  // monitor owned by the unit tests
  std::unique_ptr<MonitorMock> monitor{nullptr};

  // Thread in which monitor will be running.
  std::unique_ptr<std::thread> monitorThread{nullptr};

  // Queue for adding scirbe updates
  messaging::ReplicateQueue<LogSample> eventLogUpdatesQueue;

  // category for testing
  std::string category = "openr_new_scribe_test";
};

// Matcher macro for comparing LogSample in UT LogBasicOperation
MATCHER_P(LogSampleEq, log, "") {
  return arg.getString("event") == log.getString("event") &&
      arg.getInt("num") == log.getInt("num");
};

TEST_F(MonitorTestFixture, LogBasicOperation) {
  // Define an invalid log without event-type
  LogSample log1;
  log1.addInt("num", 100);
  // Define a valid log
  LogSample log2;
  log2.addString("event", "event_unit_test");
  log2.addInt("num", 200);

  // Expecting not to process the invalid log
  EXPECT_CALL(*monitor, processEventLog(LogSampleEq(log1))).Times(0);
  // Expecting to process the valid log once
  EXPECT_CALL(*monitor, processEventLog(LogSampleEq(log2))).Times(1);

  // Publish two logs
  eventLogUpdatesQueue.push(log1);
  eventLogUpdatesQueue.push(log2);

  // Wait for the fiber to process to get one log from list
  while (true) {
    if (monitor->getRecentEventLogs().size() == 1) {
      // Should only get the valid log from queue, discard the invalid one
      auto sample = monitor->getRecentEventLogs().front();
      EXPECT_EQ(sample.getString("event"), "event_unit_test");
      EXPECT_EQ(sample.getInt("num"), 200);
      // `domain` and `node_name` should be added to each log message
      EXPECT_FALSE(sample.getString("domain").empty());
      EXPECT_FALSE(sample.getString("node_name").empty());
      break;
    }
    std::this_thread::yield();
  }
}

int
main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  google::InstallFailureSignalHandler();

  return RUN_ALL_TESTS();
}

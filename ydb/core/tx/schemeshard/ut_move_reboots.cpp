#include <ydb/core/tx/schemeshard/ut_helpers/helpers.h>
#include <ydb/core/tx/schemeshard/schemeshard_utils.h>

#include <ydb/core/base/compile_time_flags.h>
#include <ydb/core/kqp/ut/common/kqp_ut_common.h>
#include <ydb/core/tx/datashard/change_exchange.h>

#include <util/generic/size_literals.h>
#include <util/string/cast.h>

using namespace NKikimr;
using namespace NSchemeShard;
using namespace NSchemeShardUT_Private;

Y_UNIT_TEST_SUITE(TSchemeShardMoveRebootsTest) {
    Y_UNIT_TEST(Boot) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
    }

    Y_UNIT_TEST(WithData) {
        TTestWithReboots t;

        t.Run([&](TTestActorRuntime& runtime, bool& activeZone) {
            TPathVersion pathVersion;
            {
                TInactiveZone inactive(activeZone);
                TestCreateTable(runtime, ++t.TxId, "/MyRoot", R"(
                                Name: "Table"
                                Columns { Name: "key"   Type: "Uint64" }
                                Columns { Name: "value" Type: "Utf8" }
                                KeyColumnNames: ["key"]
                                )");
                t.TestEnv->TestWaitNotification(runtime, t.TxId);

                // Write some data to the user table
                auto fnWriteRow = [&] (ui64 tabletId) {
                    TString writeQuery = R"(
                        (
                            (let key '( '('key (Uint64 '0)) ) )
                            (let value '('('value (Utf8 '281474980010683)) ) )
                            (return (AsList (UpdateRow '__user__Table key value) ))
                        )
                    )";
                    NKikimrMiniKQL::TResult result;
                    TString err;
                    NKikimrProto::EReplyStatus status = LocalMiniKQL(runtime, tabletId, writeQuery, result, err);
                    UNIT_ASSERT_VALUES_EQUAL(err, "");
                    UNIT_ASSERT_VALUES_EQUAL(status, NKikimrProto::EReplyStatus::OK);;
                };
                fnWriteRow(TTestTxConfig::FakeHiveTablets);

                pathVersion = TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::PathExist,
                                    NLs::ChildrenCount(2),
                                    NLs::ShardsInsideDomain(1)});

                auto tableVersion = TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                                                      {NLs::PathExist});
                {
                    const auto result = CompactTable(runtime, TTestTxConfig::FakeHiveTablets, tableVersion.PathId);
                    UNIT_ASSERT_VALUES_EQUAL(result.GetStatus(), NKikimrTxDataShard::TEvCompactTableResult::OK);
                }

                { //wait stats
                    TVector<THolder<IEventHandle>> suppressed;
                    auto prevObserver = SetSuppressObserver(runtime, suppressed, TEvDataShard::TEvPeriodicTableStats::EventType);

                    WaitForSuppressed(runtime, suppressed, 1, prevObserver);
                    for (auto &msg : suppressed) {
                        runtime.Send(msg.Release());
                    }
                    suppressed.clear();
                }

                TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::DatabaseSizeIs(120)});

            }

            t.TestEnv->ReliablePropose(runtime, MoveTableRequest(++t.TxId, "/MyRoot/Table", "/MyRoot/TableMove", TTestTxConfig::SchemeShard, {pathVersion}),
                                       {NKikimrScheme::StatusAccepted, NKikimrScheme::StatusMultipleModifications, NKikimrScheme::StatusPreconditionFailed});

            t.TestEnv->TestWaitNotification(runtime, t.TxId);

            {
                TInactiveZone inactive(activeZone);
                TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::ChildrenCount(2),
                                    NLs::ShardsInsideDomain(1)});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove"),
                                   {NLs::PathVersionEqual(6),
                                    NLs::IsTable});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                                   {NLs::PathNotExist});

                { //wait stats
                    TVector<THolder<IEventHandle>> suppressed;
                    auto prevObserver = SetSuppressObserver(runtime, suppressed, TEvDataShard::TEvPeriodicTableStats::EventType);

                    WaitForSuppressed(runtime, suppressed, 1, prevObserver);
                    for (auto &msg : suppressed) {
                        runtime.Send(msg.Release());
                    }
                    suppressed.clear();
                }

                TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::DatabaseSizeIs(120)});
            }
        });
    }

    Y_UNIT_TEST(WithDataAndPersistentPartitionStats) {
        TTestWithReboots t;
        t.GetTestEnvOptions().EnablePersistentPartitionStats(true);

        t.Run([&](TTestActorRuntime& runtime, bool& activeZone) {
            TPathVersion pathVersion;
            {
                TInactiveZone inactive(activeZone);
                TestCreateTable(runtime, ++t.TxId, "/MyRoot", R"(
                                Name: "Table"
                                Columns { Name: "key"   Type: "Uint64" }
                                Columns { Name: "value" Type: "Utf8" }
                                KeyColumnNames: ["key"]
                                )");
                t.TestEnv->TestWaitNotification(runtime, t.TxId);

                // Write some data to the user table
                auto fnWriteRow = [&] (ui64 tabletId) {
                    TString writeQuery = R"(
                        (
                            (let key '( '('key (Uint64 '0)) ) )
                            (let value '('('value (Utf8 '281474980010683)) ) )
                            (return (AsList (UpdateRow '__user__Table key value) ))
                        )
                    )";
                    NKikimrMiniKQL::TResult result;
                    TString err;
                    NKikimrProto::EReplyStatus status = LocalMiniKQL(runtime, tabletId, writeQuery, result, err);
                    UNIT_ASSERT_VALUES_EQUAL(err, "");
                    UNIT_ASSERT_VALUES_EQUAL(status, NKikimrProto::EReplyStatus::OK);;
                };
                fnWriteRow(TTestTxConfig::FakeHiveTablets);

                pathVersion = TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::PathExist,
                                    NLs::ChildrenCount(2),
                                    NLs::ShardsInsideDomain(1)});

                auto tableVersion = TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                                                      {NLs::PathExist});
                {
                    const auto result = CompactTable(runtime, TTestTxConfig::FakeHiveTablets, tableVersion.PathId);
                    UNIT_ASSERT_VALUES_EQUAL(result.GetStatus(), NKikimrTxDataShard::TEvCompactTableResult::OK);
                }

                { //wait stats
                    TVector<THolder<IEventHandle>> suppressed;
                    auto prevObserver = SetSuppressObserver(runtime, suppressed, TEvDataShard::TEvPeriodicTableStats::EventType);

                    WaitForSuppressed(runtime, suppressed, 1, prevObserver);
                    for (auto &msg : suppressed) {
                        runtime.Send(msg.Release());
                    }
                    suppressed.clear();
                }

                TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::DatabaseSizeIs(120)});

            }

            t.TestEnv->ReliablePropose(runtime, MoveTableRequest(++t.TxId, "/MyRoot/Table", "/MyRoot/TableMove", TTestTxConfig::SchemeShard, {pathVersion}),
                                       {NKikimrScheme::StatusAccepted, NKikimrScheme::StatusMultipleModifications, NKikimrScheme::StatusPreconditionFailed});

            TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                               {NLs::DatabaseSizeIs(120)});

            t.TestEnv->TestWaitNotification(runtime, t.TxId);

            {
                TInactiveZone inactive(activeZone);
                TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::ChildrenCount(2),
                                    NLs::ShardsInsideDomain(1)});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove"),
                                   {NLs::PathVersionEqual(6),
                                    NLs::IsTable});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                                   {NLs::PathNotExist});

                { //wait stats
                    TVector<THolder<IEventHandle>> suppressed;
                    auto prevObserver = SetSuppressObserver(runtime, suppressed, TEvDataShard::TEvPeriodicTableStats::EventType);

                    WaitForSuppressed(runtime, suppressed, 1, prevObserver);
                    for (auto &msg : suppressed) {
                        runtime.Send(msg.Release());
                    }
                    suppressed.clear();
                }

                TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::DatabaseSizeIs(120)});
            }
        });
    }

    Y_UNIT_TEST(Replace) {
        TTestWithReboots t(true);
        t.Run([&](TTestActorRuntime& runtime, bool& activeZone) {
            TPathVersion pathVersion;
            {
                TInactiveZone inactive(activeZone);
                TestCreateIndexedTable(runtime, ++t.TxId, "/MyRoot", R"(
                    TableDescription {
                      Name: "Table"
                      Columns { Name: "key"   Type: "Uint64" }
                      Columns { Name: "value" Type: "Utf8" }
                      KeyColumnNames: ["key"]
                    }
                    IndexDescription {
                      Name: "Sync"
                      KeyColumnNames: ["value"]
                    }
                )");
                TestCreateIndexedTable(runtime, ++t.TxId, "/MyRoot", R"(
                    TableDescription {
                      Name: "tmp"
                      Columns { Name: "key"   Type: "Uint64" }
                      Columns { Name: "value" Type: "Utf8" }
                      KeyColumnNames: ["key"]
                    }
                    IndexDescription {
                      Name: "Sync"
                      KeyColumnNames: ["value"]
                    }
                )");
                t.TestEnv->TestWaitNotification(runtime, {t.TxId, t.TxId - 1});


                pathVersion = TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                                 {NLs::PathExist});
            }

            ++t.TxId;
            auto first = DropTableRequest(t.TxId,  "/MyRoot", "Table");
            ++pathVersion.Version;
            auto second = MoveTableRequest(t.TxId,  "/MyRoot/tmp", "/MyRoot/Table", TTestTxConfig::SchemeShard, {pathVersion});
            auto combination = CombineSchemeTransactions({first, second});

            t.TestEnv->ReliablePropose(runtime, combination,
                                       {NKikimrScheme::StatusAccepted, NKikimrScheme::StatusMultipleModifications, NKikimrScheme::StatusPreconditionFailed});
            t.TestEnv->TestWaitNotification(runtime, t.TxId);

            {
                TInactiveZone inactive(activeZone);
                TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::ChildrenCount(2),
                                    NLs::ShardsInsideDomainOneOf({1,2,3,4})});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                                   {NLs::PathVersionEqual(6),
                                    NLs::IsTable});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/tmp"),
                                   {NLs::PathNotExist});
            }
        });
    }

    Y_UNIT_TEST(Chain) {
        TTestWithReboots t(true);
        t.Run([&](TTestActorRuntime& runtime, bool& activeZone) {
            TPathVersion pathVersion;
            {
                TInactiveZone inactive(activeZone);
                TestCreateIndexedTable(runtime, ++t.TxId, "/MyRoot", R"(
                    TableDescription {
                      Name: "Table"
                      Columns { Name: "key"   Type: "Uint64" }
                      Columns { Name: "value" Type: "Utf8" }
                      KeyColumnNames: ["key"]
                    }
                    IndexDescription {
                      Name: "Sync"
                      KeyColumnNames: ["value"]
                    }
                )");
                TestCreateIndexedTable(runtime, ++t.TxId, "/MyRoot", R"(
                    TableDescription {
                      Name: "tmp"
                      Columns { Name: "key"   Type: "Uint64" }
                      Columns { Name: "value" Type: "Utf8" }
                      KeyColumnNames: ["key"]
                    }
                    IndexDescription {
                      Name: "Sync"
                      KeyColumnNames: ["value"]
                    }
                )");
                t.TestEnv->TestWaitNotification(runtime, {t.TxId, t.TxId - 1});


                pathVersion = TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                                 {NLs::PathExist});
            }

            ++t.TxId;
            auto first = MoveTableRequest(t.TxId,  "/MyRoot/Table", "/MyRoot/backup", TTestTxConfig::SchemeShard, {pathVersion});
            auto second = MoveTableRequest(t.TxId,  "/MyRoot/tmp", "/MyRoot/Table", TTestTxConfig::SchemeShard);
            auto combination = CombineSchemeTransactions({first, second});

            t.TestEnv->ReliablePropose(runtime, combination,
                                       {NKikimrScheme::StatusAccepted, NKikimrScheme::StatusMultipleModifications, NKikimrScheme::StatusPreconditionFailed});
            t.TestEnv->TestWaitNotification(runtime, t.TxId);

            {
                TInactiveZone inactive(activeZone);

                TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                                   {NLs::ChildrenCount(3)});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/backup"),
                                   {NLs::PathVersionEqual(6),
                                    NLs::IsTable});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                                   {NLs::PathVersionEqual(6),
                                    NLs::IsTable});
                TestDescribeResult(DescribePath(runtime, "/MyRoot/tmp"),
                                   {NLs::PathNotExist});
            }
        });
    }
}


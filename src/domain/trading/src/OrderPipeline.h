#pragma once
// ---------------------------------------------------------------------------
// OrderPipeline — 纯逻辑四阶段管线（零副作用，可独立单元测试）
// ---------------------------------------------------------------------------

#include "../TradeExecutionEngine.h"  // for TradeOrder, ScheduleConflictCode, etc.

#include <string>
#include <vector>

namespace domain::trading {

class OrderSubmissionPipeline;
class ExecutionSchedulingEngine;
class OrderConflictDetector;
class ExecutionCheckpointManager;

struct PipelineResult {
    bool approved{true};
    std::string rejectReason;
    bool scheduleBlocked{false};
    ScheduleConflictCode scheduleCode{ScheduleConflictCode::None};
    std::string scheduleMessage;
    strategy::RiskRejectCode riskCode{strategy::RiskRejectCode::None};
    double riskScore{0.0};

    static PipelineResult ok() { return {}; }
    static PipelineResult rejected(std::string reason) {
        PipelineResult r; r.approved = false; r.rejectReason = std::move(reason); return r;
    }
    static PipelineResult scheduleBlock(ScheduleConflictCode code, std::string msg) {
        PipelineResult r; r.approved = false; r.scheduleBlocked = true;
        r.scheduleCode = code; r.scheduleMessage = std::move(msg); return r;
    }
    static PipelineResult riskReject(strategy::RiskRejectCode code, std::string desc) {
        PipelineResult r; r.approved = false; r.riskCode = code; r.rejectReason = std::move(desc); return r;
    }
};

class OrderPipeline {
public:
    OrderPipeline(OrderSubmissionPipeline& submissionPipeline,
                  ExecutionSchedulingEngine& schedulingEngine,
                  OrderConflictDetector& conflictDetector,
                  ExecutionCheckpointManager& checkpointManager);

    void beginBatch();
    PipelineResult submit(const TradeOrder& order, double closingPrice);
    void endBatch(std::vector<PipelineResult>& results);

private:
    OrderSubmissionPipeline& m_submissionPipeline;
    ExecutionSchedulingEngine& m_schedulingEngine;
    OrderConflictDetector& m_conflictDetector;
    ExecutionCheckpointManager& m_checkpointManager;
    std::vector<TradeOrder> m_pendingInBatch;
    std::vector<PipelineResult> m_pendingResults;
};

} // namespace domain::trading

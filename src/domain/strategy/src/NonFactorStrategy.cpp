#include "../include/NonFactorStrategy.h"
#include "../include/MultiFactorStrategy.h"
#include <ta_libc.h>
#include <cmath>
#include <memory>

namespace domain::strategy {
namespace {
using Sig = StrategyBase::SignalResult;

double rsi(const std::vector<double>& p, int per) {
    int N=(int)p.size(); if (N<per+1) return -1; std::vector<double> o(N); int b=0,n=0;
    if (TA_RSI(0,N-1,p.data(),per,&b,&n,o.data())!=TA_SUCCESS||n==0) return -1; return o[n-1];
}
bool macd(const std::vector<double>& p, bool up, int f, int s, int sp) {
    int N=(int)p.size(); if (N<s+1) return false;
    std::vector<double> m(N),g(N),h(N); int b=0,n=0;
    if (TA_MACD(0,N-1,p.data(),f,s,sp,&b,&n,m.data(),g.data(),h.data())!=TA_SUCCESS||n<2) return false;
    int pp=n-2,cc=n-1; return up?(m[pp]<=g[pp]&&m[cc]>g[cc]):(m[pp]>=g[pp]&&m[cc]<g[cc]);
}
bool maCross(const std::vector<double>& p, int f, int s, bool up) {
    int N=(int)p.size(); if (N<s+1) return false;
    std::vector<double> fm(N),sm(N); int b=0,n=0;
    if (TA_SMA(0,N-1,p.data(),f,&b,&n,fm.data())!=TA_SUCCESS||n<2) return false;
    if (TA_SMA(0,N-1,p.data(),s,&b,&n,sm.data())!=TA_SUCCESS||n<2) return false;
    return up?(fm[n-2]<=sm[n-2]&&fm[n-1]>sm[n-1]):(fm[n-2]>=sm[n-2]&&fm[n-1]<sm[n-1]);
}
struct BB { bool ok=false; double u=0,m=0,l=0; };
BB bb(const std::vector<double>& p, int per, double nd) {
    int N=(int)p.size(); if (N<per+1) return {};
    std::vector<double> u(N),m(N),l(N); int b=0,n=0;
    if (TA_BBANDS(0,N-1,p.data(),per,nd,nd,TA_MAType_SMA,&b,&n,u.data(),m.data(),l.data())!=TA_SUCCESS||n==0) return {};
    return {true,u[n-1],m[n-1],l[n-1]};
}
double atr(const std::vector<double>& p, int per) {
    int N=(int)p.size(); if (N<per+1) return 0;
    std::vector<double> t(N-1); for (int i=1;i<N;++i) t[i-1]=std::abs(p[i]-p[i-1]);
    double s=0; for (int j=N-1-per+1;j<=N-2;++j) s+=t[j]; return s/per;
}
} // anon

Sig TrendFollowingStrategy::evaluateSymbol(const std::vector<double>& p, std::uint32_t id, const RuntimeStrategyContext&) {
    Sig r; r.instrumentId=id; auto& c=m_commonCfg; int N=(int)p.size(); if (N<c.slowPeriod+1) return r;
    std::vector<double> fm(N),sm(N); int b=0,n=0;
    if (TA_SMA(0,N-1,p.data(),c.fastPeriod,&b,&n,fm.data())!=TA_SUCCESS||n<2) return r;
    if (TA_SMA(0,N-1,p.data(),c.slowPeriod,&b,&n,sm.data())!=TA_SUCCESS||n<2) return r;
    int pp=n-2,cc=n-1;
    bool g=(fm[pp]<=sm[pp]&&fm[cc]>sm[cc]), d=(fm[pp]>=sm[pp]&&fm[cc]<sm[cc]);
    double st=sm[cc]>0?(fm[cc]-sm[cc])/sm[cc]:0;
    if (g) { r.valid=true; r.isBuy=true;  r.score=0.5+std::min(0.45,std::abs(st)*10.0); }
    if (d) { r.valid=true; r.isBuy=false; r.score=0.3+std::min(0.35,std::abs(st)*10.0); }
    return r;
}
Sig MeanReversionStrategy::evaluateSymbol(const std::vector<double>& p, std::uint32_t id, const RuntimeStrategyContext&) {
    Sig r; r.instrumentId=id; double v=rsi(p,m_commonCfg.signalPeriod);
    if (v<30.0) { r.valid=true; r.isBuy=true;  r.score=0.70; }
    if (v>70.0) { r.valid=true; r.isBuy=false; r.score=0.65; } return r;
}
Sig MomentumStrategy::evaluateSymbol(const std::vector<double>& p, std::uint32_t id, const RuntimeStrategyContext&) {
    Sig r; r.instrumentId=id; auto& c=m_commonCfg;
    bool g=macd(p,true,c.macdFast,c.macdSlow,c.macdSignal), d=macd(p,false,c.macdFast,c.macdSlow,c.macdSignal);
    if (g) { r.valid=true; r.isBuy=true;  r.score=0.80; }
    if (d) { r.valid=true; r.isBuy=false; r.score=0.60; } return r;
}
Sig ArbitrageStrategy::evaluateSymbol(const std::vector<double>& p, std::uint32_t id, const RuntimeStrategyContext&) {
    Sig r; r.instrumentId=id; auto& c=m_commonCfg; auto b=bb(p,c.bbPeriod,c.bbStdDev); if (!b.ok) return r;
    double pb=(b.u>b.l)?(p.back()-b.l)/(b.u-b.l):0.5;
    if (pb<0.1) { r.valid=true; r.isBuy=true;  r.score=0.70; }
    if (pb>0.9) { r.valid=true; r.isBuy=false; r.score=0.65; } return r;
}
Sig EventDrivenStrategy::evaluateSymbol(const std::vector<double>& p, std::uint32_t id, const RuntimeStrategyContext&) {
    Sig r; r.instrumentId=id; int N=(int)p.size(); if (N<22) return r;
    double ra=0,pa=0,pv=0;
    for (int i=N-5;i<N;++i) ra+=p[i]; for (int i=N-22;i<N-5;++i) pa+=p[i]; ra/=5.0; pa/=17.0;
    for (int i=N-22;i<N-5;++i) pv+=(p[i]-pa)*(p[i]-pa); pv=std::sqrt(pv/17.0);
    double pc=(ra-pa)/(pa>0?pa:1.0), av=atr(p,14);
    if (pc>0.03&&av>pv*1.5) { r.valid=true; r.isBuy=true;  r.score=0.75; }
    if (pc<-0.03&&av>pv*1.5) { r.valid=true; r.isBuy=false; r.score=0.60; } return r;
}
Sig HighFrequencyStrategy::evaluateSymbol(const std::vector<double>& p, std::uint32_t id, const RuntimeStrategyContext&) {
    Sig r; r.instrumentId=id; int N=(int)p.size(); if (N<12) return r;
    double r3=(p[N-1]-p[N-4])/(p[N-4]>0?p[N-4]:1.0), r6=(p[N-1]-p[N-7])/(p[N-7]>0?p[N-7]:1.0);
    int ub=0; for (int i=N-5;i<N;++i) if (p[i]>p[i-1]) ++ub;
    if (r3>0.002&&r6>0.001&&ub>=4) { r.valid=true; r.isBuy=true;  r.score=0.80; }
    if (r3<-0.002&&r6<-0.001&&ub<=1) { r.valid=true; r.isBuy=false; r.score=0.75; } return r;
}
Sig CustomStrategy::evaluateSymbol(const std::vector<double>& p, std::uint32_t id, const RuntimeStrategyContext&) {
    Sig r; r.instrumentId=id; int N=(int)p.size(); if (N<26) return r; auto& c=m_commonCfg;
    auto tu=maCross(p,c.fastPeriod,c.slowPeriod,true), td=maCross(p,c.fastPeriod,c.slowPeriod,false);
    double rv=rsi(p,c.signalPeriod);
    auto mu=macd(p,true,c.macdFast,c.macdSlow,c.macdSignal), md=macd(p,false,c.macdFast,c.macdSlow,c.macdSignal);
    int bv=(tu?1:0)+(rv>0&&rv<40?1:0)+(mu?1:0), sv=(td?1:0)+(rv>60?1:0)+(md?1:0);
    if (bv>=2) { r.valid=true; r.isBuy=true;  r.score=0.70+bv*0.05; }
    if (sv>=2) { r.valid=true; r.isBuy=false; r.score=0.65+sv*0.05; } return r;
}

static ::domain::strategies::StrategyCommonConfig makeCommonCfg(const StrategyCreationParams& p) {
    ::domain::strategies::StrategyCommonConfig c;
    c.allowShort=p.allowShort; c.maxPositions=p.maxPositions;
    c.maxWeightPerStock=p.maxWeightPerStock; c.minWeightPerStock=p.minWeightPerStock;
    c.weightScheme=p.weightScheme; c.rebalanceFrequency=p.rebalanceFrequency;
    c.fastPeriod=p.fastPeriod; c.slowPeriod=p.slowPeriod; c.signalPeriod=p.signalPeriod;
    c.macdFast=p.macdFast; c.macdSlow=p.macdSlow; c.macdSignal=p.macdSignal;
    c.bbPeriod=p.bbPeriod; c.bbStdDev=p.bbStdDev;
    return c;
}

std::shared_ptr<IRuntimeStrategy> StrategyBase::create(
    StrategyInstanceId id, const StrategyCreationParams& p)
{
    auto kind = p.behaviorKind;
    auto cfg = makeCommonCfg(p);
    if (kind == ::domain::strategies::StrategyBehaviorKind::MultiFactor
        || kind == ::domain::strategies::StrategyBehaviorKind::MachineLearning) {
        MultiFactorConfig mfConfig;
        mfConfig.maxPositions = p.maxPositions > 0 ? p.maxPositions : 50;
        mfConfig.topN = p.topN > 0 ? p.topN : mfConfig.maxPositions;
        mfConfig.minCompositeScore = 0.0;
        mfConfig.sellThreshold = 0.2;
        mfConfig.industryNeutral = p.industryNeutral;
        mfConfig.weightScheme = p.weightScheme;
        mfConfig.maxWeightPerStock = p.maxWeightPerStock;
        mfConfig.minWeightPerStock = p.minWeightPerStock;
        mfConfig.skipNormalizeFactorIds = p.skipNormalizeFactorIds;
        double totalWeight = 0.0;
        for (const auto& fw : p.factorWeights) totalWeight += fw.weight;
        for (const auto& fw : p.factorWeights) {
            mfConfig.factorIds.push_back(fw.factorId);
            mfConfig.weights[fw.factorId] = totalWeight > 0.0
                ? fw.weight / totalWeight : 1.0;
        }
        return std::make_shared<MultiFactorStrategy>(id, std::move(mfConfig));
    }
    switch (kind) {
    case ::domain::strategies::StrategyBehaviorKind::TrendFollowing: return std::make_shared<TrendFollowingStrategy>(id,cfg);
    case ::domain::strategies::StrategyBehaviorKind::MeanReversion: return std::make_shared<MeanReversionStrategy>(id,cfg);
    case ::domain::strategies::StrategyBehaviorKind::Momentum:      return std::make_shared<MomentumStrategy>(id,cfg);
    case ::domain::strategies::StrategyBehaviorKind::Arbitrage:     return std::make_shared<ArbitrageStrategy>(id,cfg);
    case ::domain::strategies::StrategyBehaviorKind::EventDriven:   return std::make_shared<EventDrivenStrategy>(id,cfg);
    case ::domain::strategies::StrategyBehaviorKind::HighFrequency: return std::make_shared<HighFrequencyStrategy>(id,cfg);
    case ::domain::strategies::StrategyBehaviorKind::Custom:        return std::make_shared<CustomStrategy>(id,cfg);
    default: return std::make_shared<TrendFollowingStrategy>(id,cfg);
    }
}

} // namespace domain::strategy

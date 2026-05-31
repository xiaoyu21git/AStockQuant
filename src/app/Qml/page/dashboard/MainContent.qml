import QtQuick 2.15
import QtQuick.Layouts 1.15
import ConsoleUi 1.0

Item {
    id: mainContent
    
    // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ゆ繝鈧柆宥呯劦妞ゆ帒鍊归崵鈧柣搴㈠嚬閸欏啫鐣峰畷鍥ь棜閻庯絻鍔嬪Ч妤呮⒑閸︻厼鍔嬮柛銊ョ秺瀹曟劙鎮欏顔藉瘜闂侀潧鐗嗗Λ妤冪箔閹烘挶浜滈柨鏂跨仢瀹撳棛鈧鍠楅悡锟犮€侀弮鍫濋唶闁绘棁娓归悽缁樼節閻㈤潧孝闁挎洏鍊濆畷顖炲箥椤斿彞绗夌紓鍌欑劍閿曗晛鈻撴禒瀣厽闁归偊鍘界紞鎴︽煟韫囨梹缍戦柍瑙勫灴椤㈡瑩鎮锋０浣割棜闂傚倸鍊风欢姘焽瑜旈幃褔宕卞☉妯肩枃闂侀€涘嵆閸嬪﹪寮繝鍌楁斀闁绘ɑ褰冮埀顒傛暬瀵劍绂掔€ｎ亞顔婇梺瑙勫劶濡嫮澹曡ぐ鎺撶厵闁绘鐗婄欢鑼棯閹岀吋闁哄瞼鍠栭獮鍡氼槻妞わ綀娅曟穱濠囶敃椤愩垻浠搁梺鍝勭灱閸犳牠銆佸☉銏犲耿婵°倕鍟版导鍥⒑閸涘﹨澹樻い鎴濐槸椤繐煤椤忓嫪绱堕梺鍛婃处閸橀箖宕濋崷顓犵＝闁稿本姘ㄥ皬缂備讲鍋撳〒姘ｅ亾妞ゃ垺宀搁弫鎰緞濡粯娅旈梻渚€鈧偛鑻晶顕€鏌ｉ敐鍥у幋妤犵偞甯￠獮瀣敇閻斿嘲鍘炲┑锛勫亼閸婃牠宕归悡搴樻灃闁哄洨濮甸弳婊勭箾閹寸偞鐨戠痪鍙ョ矙閺屾稓浠﹂崜褎鍣梺绋跨箰閻倿寮诲☉姘ｅ亾閿濆骸浜濈€规洖鐬奸埀顒冾潐濞叉ê煤閻旇偐宓侀幖杈剧稻閸犲棝鏌ㄥ┑鍡椻偓鍛婄妤ｅ啯鐓ラ柡鍥╁仜閳ь剙缍婂畷鎰節濮橆厾鍙冨┑鈽嗗灟鐠€锕€危婵傚憡鐓欓柤鎭掑劜缁€瀣叏婵犲啯銇濇鐐村姈閹棃鏁愰崶鈺傛濠碉紕鍋戦崐褏鈧潧鐭傚畷鐟扮暦閸パ冪亰闁荤姴娲︾粊鏉懳ｉ崼鐔虹闁糕剝顨堝皬缂備降鍔岄…宄邦潖濞差亝鍋￠柣妤€鐗婇崕鎾剁磽娴ｅ壊妲搁柣鏍帶閻ｅ嘲鈹戦崱鈺佹倯婵犮垼娉涢鍥储闁秵鐓欓柛蹇氬亹閺嗘﹢鏌涢妸銉︽儓闁伙綁顥撴禒锕傛倷椤掆偓瀵?
    property string pageMode: "dashboard"
    property string currentMenuCode: "live_trading"
    property var marketDataService: null
    property var positionAccountService: null
    property var tradeExecutionService: null
    property var strategyService: null
    property var marketData: []
    property var statusCards: []
    property var positions: []
    property var strategies: []
    property var liveAccountOrderStatusesCache: []
    property var liveMarketSnapshotCache: []
    property int liveStrategySyncVersion: 0
    readonly property bool isTradeRecordsView: pageMode === "live-trading" && currentMenuCode === "trade_records"
    readonly property bool isPerformanceAnalysisView: pageMode === "live-trading" && currentMenuCode === "performance_analysis"
    readonly property var liveAccountSnapshot: pageMode === "live-trading" && positionAccountService ? positionAccountService.accountSnapshot : ({})
    readonly property var liveRecentOrders: pageMode === "live-trading"
        ? ((liveAccountOrderStatusesCache && liveAccountOrderStatusesCache.length > 0)
            ? liveAccountOrderStatusesCache
            : (tradeExecutionService ? tradeExecutionService.recentOrders : []))
        : []
    readonly property var liveTradeRecordOrders: pageMode === "live-trading" ? prioritizeTradeRecordOrders(liveRecentOrders) : []
    readonly property var effectiveMarketData: pageMode === "live-trading" && liveMarketSnapshotCache && liveMarketSnapshotCache.length > 0
        ? liveMarketSnapshotCache
        : marketData
    readonly property var livePrimaryQuote: findMarketSnapshot(livePrimarySymbol)
    readonly property string liveOverviewChartSymbol: {
        if (pageMode !== "live-trading") {
            return livePrimarySymbol
        }
        if (marketDataService && marketDataService.primarySymbol) {
            return String(marketDataService.primarySymbol)
        }
        if (effectiveMarketData.length > 0 && effectiveMarketData[0].symbol) {
            return String(effectiveMarketData[0].symbol)
        }
        return ""
    }
    readonly property string livePrimarySymbol: {
        if (liveRecentOrders.length > 0 && liveRecentOrders[0].symbol) {
            return String(liveRecentOrders[0].symbol)
        }
        if (effectivePositions.length > 0 && effectivePositions[0].symbol) {
            return String(effectivePositions[0].symbol)
        }
        if (pageMode === "live-trading" && marketDataService && marketDataService.primarySymbol) {
            return String(marketDataService.primarySymbol)
        }
        if (effectiveMarketData.length > 0 && effectiveMarketData[0].symbol) {
            return String(effectiveMarketData[0].symbol)
        }
        return ""
    }
    readonly property color riseColor: "#ef4444"
    readonly property color fallColor: "#10b981"
    readonly property color neutralAccentColor: "#3b82f6"
    readonly property string displayCurrencySymbol: pageMode === "live-trading" ? "\u00a5" : "$"

    function currencyText(value) {
        return displayCurrencySymbol + Number(value || 0).toLocaleString(Qt.locale(), 'f', 2)
    }


    function signedCurrencyText(value) {
        var numericValue = Number(value || 0)
        return (numericValue >= 0 ? "+" : "-") + displayCurrencySymbol + Math.abs(numericValue).toLocaleString(Qt.locale(), 'f', 2)
    }

    function liveText(key) {
        var translations = {
            accountAssets: "\u8d44\u4ea7\u603b\u989d",
            availableCash: "\u53ef\u7528\u8d44\u91d1",
            marketValue: "\u6301\u4ed3\u5e02\u503c",
            marketValueRatio: "\u6301\u4ed3\u5360\u6bd4",
            realizedPnl: "\u5df2\u5b9e\u73b0\u76c8\u4e8f",
            unrealizedPnl: "\u6d6e\u52a8\u76c8\u4e8f",
            latestOrder: "\u6700\u65b0\u59d4\u6258",
            noOrderUpdates: "\u6682\u65e0\u59d4\u6258\u66f4\u65b0",
            orderSyncReady: "\u8d26\u6237\u59d4\u6258\u5df2\u540c\u6b65",
            noAccountOrders: "\u6682\u65e0\u8d26\u6237\u59d4\u6258\u72b6\u6001",
            tradesCount: "\u6210\u4ea4\u7b14\u6570",
            positionsCount: "\u6301\u4ed3\u6570\u91cf",
            turnover: "\u6210\u4ea4\u989d",
            unfinishedOrders: "\u672a\u5b8c\u6210\u59d4\u6258",
            totalMarketValue: "\u603b\u5e02\u503c",
            leadersGain: "\u9886\u6da8\u6807\u7684",
            leadersLoss: "\u9886\u8dcc\u6807\u7684",
            watchlistFocus: "\u5173\u6ce8\u6807\u7684",
            noPositionData: "\u6301\u4ed3 / \u5f53\u524d\u65e0\u6301\u4ed3",
            positionStatusPrefix: "\u6301\u4ed3 / ",
            positionStatusSuffix: " \u53ea / ",
            sharesUnit: "\u80a1",
            currentPrice: "\u6700\u65b0\u4ef7",
            floatingPnlLabel: "\u6d6e\u76c8",
            orderCountLabel: "\u7b14\u6570",
            symbolLabel: "\u6807\u7684",
            statusLabel: "\u72b6\u6001",
            orderIdLabel: "\u59d4\u6258\u53f7",
            timeLabel: "\u65f6\u95f4",
            pendingSymbol: "\u5f85\u5904\u7406\u6807\u7684",
            unnamedStrategy: "\u672a\u547d\u540d\u7b56\u7565",
            strategySync: "\u7b56\u7565\u670d\u52a1\u540c\u6b65",
            home: "\u9996\u9875",
            tradingDesk: "\u4ea4\u6613\u5de5\u4f5c\u53f0",
            liveTrading: "\u5b9e\u76d8\u4ea4\u6613",
            tradeExecution: "\u59d4\u6258\u6267\u884c",
            tradeTesting: "\u4ea4\u6613\u6d4b\u8bd5",
            positionManagement: "\u6301\u4ed3\u7ba1\u7406",
            fundManagement: "\u8d44\u91d1\u7ba1\u7406",
            tradeRecords: "\u4ea4\u6613\u8bb0\u5f55",
            performanceAnalysis: "\u7ee9\u6548\u5206\u6790",
            performanceBreakdown: "\u7ee9\u6548\u62c6\u89e3",
            liveBroadcast: "\u5b9e\u76d8\u64ad\u62a5"
        }
        return translations[key] || key
    }
    function pnlAccentColor(value) {
        return Number(value || 0) >= 0 ? riseColor : fallColor
    }

    function pnlAccentBackground(value) {
        return Number(value || 0) >= 0 ? "#ef444420" : "#10b98120"
    }

    function pnlAccentBorder(value) {
        return Number(value || 0) >= 0 ? "#ef444455" : "#10b98155"
    }

    function orderSideAccentColor(side) {
        var sideText = String(side || "").toUpperCase()
        if (sideText === "BUY") {
            return riseColor
        }
        if (sideText === "SELL") {
            return fallColor
        }
        return neutralAccentColor
    }

    function orderSideAccentBackground(side) {
        var sideText = String(side || "").toUpperCase()
        if (sideText === "BUY") {
            return "#ef444420"
        }
        if (sideText === "SELL") {
            return "#10b98120"
        }
        return "#3b82f620"
    }

    function orderSideAccentBorder(side) {
        var sideText = String(side || "").toUpperCase()
        if (sideText === "BUY") {
            return "#ef444455"
        }
        if (sideText === "SELL") {
            return "#10b98155"
        }
        return "#3b82f655"
    }

    function orderStatusValueColor(status) {
        var statusText = normalizedOrderStatus(status)
        if (statusText === "SUBMITTED") {
            return "#60a5fa"
        }
        if (statusText === "PENDING") {
            return "#38bdf8"
        }
        if (statusText === "PARTIAL_FILLED") {
            return "#f59e0b"
        }
        if (statusText === "FILLED") {
            return "#22c55e"
        }
        if (statusText === "CANCELLED") {
            return "#94a3b8"
        }
        if (statusText === "REJECTED") {
            return "#ef4444"
        }
        return "#f1f5f9"
    }

    readonly property var effectiveStatusCards: pageMode === "live-trading" ? [
        {
            title: liveText("accountAssets"),
            value: currencyText(liveAccountSnapshot.totalAsset || 0),
            detail: liveText("availableCash") + " " + currencyText(liveAccountSnapshot.availableCash || 0),
            iconSource: "qrc:/resources/icons/chart-line.svg",
            accentColor: neutralAccentColor,
            accentBackground: "#3b82f620",
            accentBorder: "#3b82f655",
            indicatorText: "\u8d44",
            valueColor: "#f1f5f9",
            detailColor: "#94a3b8"
        },
        {
            title: liveText("marketValue"),
            value: currencyText(liveAccountSnapshot.marketValue || 0),
            detail: liveText("marketValueRatio") + " " + Number((liveAccountSnapshot.totalAsset || 0) > 0 ? (liveAccountSnapshot.marketValue || 0) / liveAccountSnapshot.totalAsset * 100 : 0).toFixed(1) + "%",
            iconSource: "qrc:/resources/icons/100.svg",
            accentColor: "#0ea5a4",
            accentBackground: "#14b8a620",
            accentBorder: "#14b8a655",
            indicatorText: "\u4ed3",
            valueColor: "#f1f5f9",
            detailColor: "#94a3b8"
        },
        {
            title: liveText("realizedPnl"),
            value: signedCurrencyText(liveAccountSnapshot.realizedPnl || 0),
            detail: liveText("unrealizedPnl") + " " + signedCurrencyText(liveAccountSnapshot.unrealizedPnl || 0),
            iconSource: "qrc:/resources/icons/shield-alt.svg",
            accentColor: pnlAccentColor(liveAccountSnapshot.realizedPnl || 0),
            accentBackground: pnlAccentBackground(liveAccountSnapshot.realizedPnl || 0),
            accentBorder: pnlAccentBorder(liveAccountSnapshot.realizedPnl || 0),
            indicatorText: Number(liveAccountSnapshot.realizedPnl || 0) >= 0 ? "+" : "-",
            valueColor: pnlAccentColor(liveAccountSnapshot.realizedPnl || 0),
            detailColor: pnlAccentColor(liveAccountSnapshot.unrealizedPnl || 0)
        },
        {
            title: liveText("latestOrder"),
            value: liveRecentOrders.length > 0 ? displayOrderStatus(liveRecentOrders[0].status) : "--",
            detail: liveRecentOrders.length > 0 ? String((liveRecentOrders[0].symbol || "") + " / " + displayOrderSide(liveRecentOrders[0].side)) : liveText("noOrderUpdates"),
            iconSource: "qrc:/resources/icons/robot.svg",
            accentColor: liveRecentOrders.length > 0 ? orderSideAccentColor(liveRecentOrders[0].side) : neutralAccentColor,
            accentBackground: liveRecentOrders.length > 0 ? orderSideAccentBackground(liveRecentOrders[0].side) : "#3b82f620",
            accentBorder: liveRecentOrders.length > 0 ? orderSideAccentBorder(liveRecentOrders[0].side) : "#3b82f655",
            indicatorText: liveRecentOrders.length > 0 ? displayOrderSide(liveRecentOrders[0].side).slice(0, 1) : "\u59d4",
            valueColor: liveRecentOrders.length > 0 ? orderStatusValueColor(liveRecentOrders[0].status) : "#f1f5f9",
            detailColor: liveRecentOrders.length > 0 ? orderSideAccentColor(liveRecentOrders[0].side) : "#94a3b8"
        }
    ] : statusCards
    readonly property var effectivePositions: pageMode === "live-trading"
        ? (positionAccountService && positionAccountService.positions
            ? positionAccountService.positions.map(function(position) {
                var quantity = Number(position.quantity || position.shares || 0)
                var availableQuantity = Number(position.availableQuantity || position.availableShares || quantity)
                var avgPrice = Number(position.costBasis || position.avgPrice || 0)
                var snapshot = findMarketSnapshot(position.symbol || "")
                var lastPrice = Number(snapshot.price || position.lastPrice || avgPrice || 0)
                var currentValue = lastPrice > 0 ? quantity * lastPrice : Number(position.marketValue || position.currentValue || 0)
                var pnl = currentValue - quantity * avgPrice
                return {
                    symbol: position.symbol || "",
                    name: snapshot.name || position.name || position.symbol || "",
                    shares: quantity,
                    availableQuantity: availableQuantity,
                    avgPrice: avgPrice,
                    lastPrice: lastPrice,
                    currentValue: currentValue,
                    pnl: pnl,
                    pnlRate: quantity > 0 && avgPrice > 0 ? pnl / (quantity * avgPrice) * 100 : 0,
                    weight: Number(liveAccountSnapshot.marketValue || 0) > 0 ? currentValue / Number(liveAccountSnapshot.marketValue || 0) * 100 : 0,
                    change: Number(snapshot.change || 0),
                    updatedAt: snapshot.updatedAt || position.updatedAt || "--",
                    color: snapshot.color || "#3b82f6"
                }
            }).filter(function(position) {
                return Number(position.shares || 0) > 0
            }).sort(function(left, right) {
                return Number(right.currentValue || 0) - Number(left.currentValue || 0)
            })
            : [])
        : positions
    readonly property var liveMarketSections: pageMode === "live-trading" ? buildLiveMarketSections() : []
    readonly property real livePositionCount: effectivePositions ? effectivePositions.length : 0
    readonly property int liveAccountOrderCount: pageMode === "live-trading" && liveAccountOrderStatusesCache
        ? liveAccountOrderStatusesCache.length
        : 0
    readonly property string liveLatestAccountOrderId: liveAccountOrderCount > 0 && liveAccountOrderStatusesCache[0].orderId
        ? String(liveAccountOrderStatusesCache[0].orderId)
        : "--"
    readonly property string liveLatestAccountOrderTime: liveAccountOrderCount > 0
        ? String(liveAccountOrderStatusesCache[0].updatedAt || liveAccountOrderStatusesCache[0].createdAt || "--")
        : "--"
    readonly property string liveLatestAccountOrderStatus: liveAccountOrderCount > 0
        ? String(liveAccountOrderStatusesCache[0].status || "--")
        : "--"
    readonly property string liveLatestAccountOrderSymbol: liveAccountOrderCount > 0
        ? String(liveAccountOrderStatusesCache[0].symbol || "--")
        : "--"
    readonly property string liveLatestAccountOrderMessage: liveAccountOrderCount > 0
        ? String(liveAccountOrderStatusesCache[0].message || "")
        : ""
    readonly property string liveAccountOrderSyncState: liveAccountOrderCount > 0 ? liveText("orderSyncReady") : liveText("noAccountOrders")
    readonly property string livePositionTickerText: buildPositionTickerText()
    readonly property string liveMarqueeText: buildLiveMarqueeText()
    readonly property real liveFilledOrderCount: liveRecentOrders ? liveRecentOrders.filter(function(order) {
        return String(order.status || "").toUpperCase() === "FILLED"
    }).length : 0
    readonly property real liveSubmittedOrderCount: liveRecentOrders ? liveRecentOrders.filter(function(order) {
        return String(order.status || "").toUpperCase() === "SUBMITTED"
    }).length : 0
    readonly property real liveTurnover: liveRecentOrders ? liveRecentOrders.reduce(function(sum, order) {
        return sum + Number(order.filledNotional || order.requestedNotional || (Number(order.price || 0) * Number(order.quantity || 0)))
    }, 0) : 0
    readonly property real liveUnfinishedOrderCount: liveRecentOrders ? liveRecentOrders.filter(function(order) {
        return isUnfinishedOrderStatus(order.status)
    }).length : 0
    readonly property var performanceCards: [
        {
            title: liveText("tradesCount"),
            value: String(liveFilledOrderCount),
            detail: liveText("latestOrder") + " " + String(liveSubmittedOrderCount)
        },
        {
            title: liveText("positionsCount"),
            value: String(livePositionCount),
            detail: liveText("totalMarketValue") + " " + currencyText(liveAccountSnapshot.marketValue || 0)
        },
        {
            title: liveText("turnover"),
            value: currencyText(liveTurnover),
            detail: liveText("accountAssets") + " " + currencyText(liveAccountSnapshot.totalAsset || 0)
        },
        {
            title: liveText("realizedPnl"),
            value: signedCurrencyText(liveAccountSnapshot.realizedPnl || 0),
            detail: liveText("unrealizedPnl") + " " + currencyText(liveAccountSnapshot.unrealizedPnl || 0)
        }
    ]
    readonly property var effectiveStrategies: pageMode === "live-trading" ? buildLiveStrategies() : strategies

    function normalizeStrategyStatus(strategy) {
        var statusText = String(strategy.status || strategy.strategy_status || strategy.state || "").toLowerCase()
        if (statusText === "running" || statusText === "active" || statusText === "enabled") {
            return "running"
        }
        if (statusText === "paused" || statusText === "inactive" || statusText === "disabled") {
            return "paused"
        }
        if (strategy.isActive === true || strategy.enabled === true) {
            return "running"
        }
        return statusText ? "paused" : "draft"
    }

    function findMarketSnapshot(symbol) {
        var normalizedSymbol = String(symbol || "")
        for (var index = 0; index < effectiveMarketData.length; ++index) {
            var entry = effectiveMarketData[index] || ({})
            if (String(entry.symbol || "") === normalizedSymbol) {
                return entry
            }
        }
        return ({ symbol: normalizedSymbol })
    }

    function formatInstrumentLabel(symbol, name) {
        var symbolText = String(symbol || "--")
        var nameText = String(name || "")
        return nameText.length > 0 ? (symbolText + " " + nameText) : symbolText
    }

    function marketInstrumentLabel(symbol) {
        var snapshot = findMarketSnapshot(symbol)
        return formatInstrumentLabel(snapshot.symbol || symbol || "--", snapshot.name || "")
    }

    function normalizedOrderStatus(status) {
        return String(status || "").toUpperCase()
    }

    function displayOrderStatus(status) {
        var statusText = normalizedOrderStatus(status)
        if (statusText === "SUBMITTED") {
            return "\u5df2\u63d0\u4ea4"
        }
        if (statusText === "PENDING") {
            return "\u5f85\u6210\u4ea4"
        }
        if (statusText === "PARTIAL_FILLED") {
            return "\u90e8\u5206\u6210\u4ea4"
        }
        if (statusText === "FILLED") {
            return "\u5df2\u6210\u4ea4"
        }
        if (statusText === "CANCELLED") {
            return "\u5df2\u64a4\u5355"
        }
        if (statusText === "REJECTED") {
            return "\u5df2\u62d2\u5355"
        }
        return statusText || "--"
    }

    function displayOrderSide(side) {
        var sideText = String(side || "").toUpperCase()
        if (sideText === "BUY") {
            return "\u4e70\u5165"
        }
        if (sideText === "SELL") {
            return "\u5356\u51fa"
        }
        return sideText || "--"
    }

    function isUnfinishedOrderStatus(status) {
        var statusText = normalizedOrderStatus(status)
        return statusText === "SUBMITTED" || statusText === "PENDING" || statusText === "PARTIAL_FILLED"
    }

    function prioritizeTradeRecordOrders(orders) {
        if (!orders || orders.length === 0) {
            return []
        }

        var copiedOrders = orders.slice(0)
        copiedOrders.sort(function(left, right) {
            var leftOpen = isUnfinishedOrderStatus(left.status) ? 0 : 1
            var rightOpen = isUnfinishedOrderStatus(right.status) ? 0 : 1
            if (leftOpen !== rightOpen) {
                return leftOpen - rightOpen
            }

            var leftTime = String(left.updatedAt || left.filledAt || left.createdAt || "")
            var rightTime = String(right.updatedAt || right.filledAt || right.createdAt || "")
            if (leftTime === rightTime) {
                return 0
            }
            return leftTime > rightTime ? -1 : 1
        })

        return copiedOrders
    }

    function syncLiveAccountOrderStatusesCache() {
        liveAccountOrderStatusesCache = positionAccountService && positionAccountService.recentOrderStatuses
            ? positionAccountService.recentOrderStatuses
            : []
    }

    function syncLiveMarketSnapshotCache() {
        var snapshots = []
        if (pageMode === "live-trading" && marketDataService && marketDataService.marketSnapshots) {
            snapshots = marketDataService.marketSnapshots
        }
        liveMarketSnapshotCache = snapshots && snapshots.length > 0 ? snapshots : []
    }

    function buildLiveMarketSections() {
        if (!effectiveMarketData || effectiveMarketData.length === 0) {
            return []
        }

        var snapshots = effectiveMarketData.slice(0).filter(function(entry) {
            return !!entry && String(entry.symbol || "").length > 0
        })

        var gainers = snapshots.slice(0).sort(function(left, right) {
            return Number(right.change || 0) - Number(left.change || 0)
        }).slice(0, 3)

        var losers = snapshots.slice(0).sort(function(left, right) {
            return Number(left.change || 0) - Number(right.change || 0)
        }).slice(0, 3)

        var focusSymbols = []
        for (var positionIndex = 0; positionIndex < effectivePositions.length; ++positionIndex) {
            var positionSymbol = String((effectivePositions[positionIndex] || {}).symbol || "")
            if (positionSymbol.length > 0 && focusSymbols.indexOf(positionSymbol) < 0) {
                focusSymbols.push(positionSymbol)
            }
        }
        for (var orderIndex = 0; orderIndex < liveRecentOrders.length; ++orderIndex) {
            var orderSymbol = String((liveRecentOrders[orderIndex] || {}).symbol || "")
            if (orderSymbol.length > 0 && focusSymbols.indexOf(orderSymbol) < 0) {
                focusSymbols.push(orderSymbol)
            }
            if (focusSymbols.length >= 3) {
                break
            }
        }

        var focus = []
        for (var focusIndex = 0; focusIndex < focusSymbols.length; ++focusIndex) {
            var focusEntry = findMarketSnapshot(focusSymbols[focusIndex])
            if (focusEntry.symbol) {
                focus.push(focusEntry)
            }
        }
        if (focus.length === 0) {
            focus = gainers.slice(0, 3)
        }

        return [
            { title: liveText("leadersGain"), icon: "\u6da8", stocks: gainers },
            { title: liveText("leadersLoss"), icon: "\u8dcc", stocks: losers },
            { title: liveText("watchlistFocus"), icon: "\u7126", stocks: focus }
        ]
    }

    function buildPositionTickerText() {
        if (!effectivePositions || effectivePositions.length === 0) {
            return liveText("noPositionData")
        }

        var entries = []
        for (var index = 0; index < Math.min(effectivePositions.length, 3); ++index) {
            var position = effectivePositions[index] || ({})
            var pnlValue = Number(position.pnl || 0)
            entries.push(
                String(position.symbol || "--")
                + (position.name ? (" " + String(position.name)) : "")
                + " " + String(Number(position.shares || 0)) + liveText("sharesUnit")
                + " " + liveText("currentPrice") + " " + currencyText(position.lastPrice || 0)
                + " " + liveText("floatingPnlLabel") + " " + signedCurrencyText(pnlValue)
            )
        }

        return liveText("positionStatusPrefix") + String(effectivePositions.length) + liveText("positionStatusSuffix") + entries.join(" / ")
    }

    function buildLiveMarqueeText() {
        var segments = []
        if (livePositionTickerText.length > 0) {
            segments.push(livePositionTickerText)
        }

        if (liveAccountOrderCount > 0) {
            segments.push(
                mainContent.liveAccountOrderSyncState
                + " / " + liveText("orderCountLabel") + " " + String(mainContent.liveAccountOrderCount)
                + " / " + liveText("symbolLabel") + " " + mainContent.liveLatestAccountOrderSymbol
                + " / " + liveText("statusLabel") + " " + mainContent.displayOrderStatus(mainContent.liveLatestAccountOrderStatus)
                + " / " + liveText("orderIdLabel") + " " + mainContent.liveLatestAccountOrderId
                + " / " + liveText("timeLabel") + " " + mainContent.liveLatestAccountOrderTime
                + (liveLatestAccountOrderMessage.length > 0 ? (" / " + liveLatestAccountOrderMessage) : "")
            )
        } else {
            segments.push(liveAccountOrderSyncState)
        }

        return segments.join("   |   ")
    }

    function buildLiveStrategies() {
        var syncToken = liveStrategySyncVersion
        if (!strategyService || typeof strategyService.getAllStrategies !== "function") {
            return strategies
        }

        var rawStrategies = strategyService.getAllStrategies() || []
        var normalizedStrategies = []
        for (var index = 0; index < rawStrategies.length; ++index) {
            var rawStrategy = rawStrategies[index] || ({})
            var performance = rawStrategy.performance || ({})
            var parameters = rawStrategy.parameters || ({})
            var symbols = parameters.symbols || []
            var symbolText = liveText("pendingSymbol")
            if (Array.isArray(symbols) && symbols.length > 0) {
                symbolText = symbols.slice(0, 2).join(", ")
            } else if (typeof symbols === "string" && symbols.length > 0) {
                symbolText = symbols
            } else if (rawStrategy.symbol) {
                symbolText = String(rawStrategy.symbol)
            }

            normalizedStrategies.push({
                strategyId: rawStrategy.strategyId || "",
                name: rawStrategy.strategyName || liveText("unnamedStrategy"),
                status: normalizeStrategyStatus(rawStrategy),
                subtitle: (rawStrategy.strategyCode || rawStrategy.strategyType || liveText("strategySync")) + " / " + symbolText,
                returns: Number(performance.totalReturn || performance.returnRate || rawStrategy.returnRate || 0),
                trades: Number(performance.totalTrades || performance.tradeCount || rawStrategy.tradeCount || 0),
                syncToken: syncToken
            })
        }

        return normalizedStrategies
    }

    Component.onCompleted: {
        syncLiveAccountOrderStatusesCache()
        syncLiveMarketSnapshotCache()
    }

    onPageModeChanged: syncLiveMarketSnapshotCache()
    onMarketDataServiceChanged: syncLiveMarketSnapshotCache()
    onVisibleChanged: {
        if (visible) {
            syncLiveMarketSnapshotCache()
        }
    }

    Connections {
        target: mainContent.marketDataService
        enabled: mainContent.visible && !!mainContent.marketDataService

        function onMarketSnapshotsChanged() {
            mainContent.syncLiveMarketSnapshotCache()
        }
    }

    Connections {
        target: mainContent.positionAccountService
        enabled: mainContent.visible && !!mainContent.positionAccountService

        function onRecentOrderStatusesChanged() {
            mainContent.syncLiveAccountOrderStatusesCache()
        }
    }

    Connections {
        target: mainContent.strategyService
        enabled: mainContent.visible && !!mainContent.strategyService

        function onStrategiesLoaded() {
            mainContent.liveStrategySyncVersion += 1
        }

        function onDataChanged() {
            mainContent.liveStrategySyncVersion += 1
        }
    }

    function breadcrumbText() {
        if (pageMode !== "live-trading") {
            return liveText("home") + " / " + liveText("tradingDesk") + " / " + liveText("liveTrading")
        }
        var menuNames = {
            trade_execution: liveText("tradeExecution"),
            position_management: liveText("positionManagement"),
            fund_management: liveText("fundManagement"),
            trade_records: liveText("tradeRecords"),
            performance_analysis: liveText("performanceAnalysis"),
            live_trading: liveText("liveTrading")
        }
        return liveText("home") + " / " + liveText("liveTrading") + " / " + (menuNames[currentMenuCode] || liveText("liveTrading"))
    }
    
    Rectangle {
        anchors.fill: parent
        color: "#0a0f1a"
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            
            // 濠电姷鏁告慨鐑藉极閸涘﹥鍙忛柣鎴ｆ閺嬩線鏌涘☉姗堟敾闁告瑥绻橀弻锝夊箣閿濆棭妫勯梺鍝勵儎缁舵岸寮诲☉妯锋婵鐗婇弫楣冩⒑閸涘﹦鎳冪紒缁橈耿瀵鏁愭径濠勵吅濠电姴鐏氶崝鏍礊濡ゅ懏鈷戦梺顐ゅ仜閼活垱鏅堕鈧弻娑欑節閸屾稑浠撮梺闈涙缁€渚€鍩㈡惔銊ョ闁哄鍨熼崑鎾绘煥鐎ｃ劋绨婚梺鍦劋閸╁牆危閸︻厾纾界€广儱妫▓婊勬叏婵犲嫮甯涢柟宄版嚇瀹曘劍绻濋崘銊ュ濠电姷鏁搁崑娑㈡儑娴兼潙鍨傞柦妯侯槺閺嗭妇鎲搁悧鍫濈瑨闁圭鍩栭妵鍕箻鐠哄搫澹夊┑鈽嗗亝缁秶鎹㈠┑瀣仺闂傚牊鍒€閿濆洨妫柡澶庢硶鏁堥梺璇″枓閳ь剚鏋奸弸搴ㄦ煙閹咃紞濡ょ姴娲娲礈閹绘帊绨肩紓浣筋嚙閸婂潡宕哄☉銏犵睄闁割偆鍠撻崢浠嬫⒑閹稿海绠撻柣妤€鎳樺畷銉╊敃閿旂晫鍘介梺闈涱樈閸犳洟鏌囬娑辨闁绘劕寮堕ˉ鈩冦亜閿曗偓椤曨厾妲愰幒鎾崇窞閻庯綆鍋佹禒銏ゆ⒑閸濆嫬顦柍褜鍓欑壕顓㈠汲閸℃稒鍊甸柨婵嗛婢т即鏌ｉ敃鈧悧鎾诲箖濡ゅ啯鍠嗛柛鏇ㄥ墰椤︺儵姊虹紒妯煎ⅹ闁告艾顑嗙粚杈ㄧ節閸ャ劌鈧鏌﹀Ο鐚寸礆闁靛ě鍕瀾閻庡厜鍋撻柛鏇ㄥ墮閸擄箑顪冮妶鍛闁绘锕崺娑㈠箳閹炽劌缍婇弫鎰板礋椤曞懎濡抽梻浣虹帛鐢偤宕戦幘璇参﹂柛鏇ㄥ灠缁狅絾绻濋棃娑欐悙婵炲牊顨婇弻锕€螣閼姐倗鐣洪梺闈涙搐鐎氭澘顕ｆ禒瀣垫晝妞ゎ偒鍘鹃幑鏇㈡⒒娴ｈ櫣甯涢柟绋款煼閺佸啴濮€閵堝懓鎽曢梺缁樻煥閹诧紕绱為崶顒佺厱闁圭偓顨呴幊蹇撯枔閸濄儳纾藉ù锝呮惈娴滈箖鏌涙惔銏犫枙鐎规洏鍎抽埀顒婄秵閸撴艾鐣烽崣澶岀瘈闂傚牊绋掑婵堢磼閳锯偓閸嬫捇姊绘担渚劸闁哄牜鍓涚划娆撳箳閹寸姵娈鹃梺瑙勫劶婵倝鍩涢幋锔界厱婵炴垶锕弨濠氭煟閹惧崬鍔﹂柡宀€鍠栭幊鐐哄Ψ瑜忛悡鍌氣攽閳ュ啿绾ч柛鏃€鐟╅悰顕€骞掗幊铏⒐閹峰懐鍖栭弴鐐板闂備礁鐏濋鍐╃濠婂牊鐓欓柛婵嗗椤ユ粌霉濠婂牏鐣烘慨濠冩そ瀹曘劍绻濋崘顏勫箺闂備胶顭堥敃銉╂偋濠婂牆绠查柕蹇曞Л濡插牊鎱ㄥ鍡楀箹闁告顫夋穱濠囨倷瀹割喖鍓板┑鐐差嚟閸忔﹢骞嗘笟鈧畷鐓庮熆椤忓懏顥堥柟顔规櫊濡啫鈽夊Δ鍐╁礋闂傚倷绀侀幗婊堝闯閵夆晛纾诲鑸靛姈閸婂爼鏌涢幇闈涙灍闁抽攱鍨块弻娑樷槈濮楀牊顣肩紓浣哥埣娴滃爼寮诲☉銏″亹鐎规洖娲ら～褏绱撴担绋库偓鍦暜閹烘柨寮叉俊鐐€曠换鎰板箠婢舵劕绠柛婵嗗閺€鑺ャ亜閺冨倸浜鹃柛銈傚亾婵犵數鍋涢惇浼村磹濠靛绠栨慨妞诲亾妞ゃ垺宀搁崺鈧い鎺戝閽冪喐绻涢幋鐐茬劰闁稿鎹囬弫鎰板川椤撗勬缂傚倷绀侀ˇ浠嬪闯閿濆钃熸繛鎴欏灩閻撴﹢鏌熼鍡楀€搁ˉ姘節绾板纾块柛瀣灴瀹曟劙寮借閸熷懎鈹戦悩瀹犲缁炬儳顭烽弻鐔煎礈瑜忕敮娑㈡煟閹惧崬鍔﹂柡宀嬬節瀹曞爼濡烽妷銉€遍梻鍌欑贰閸欏酣宕归幎钘夌劦妞ゆ帊绶￠崯蹇涙煕閻樺磭娲存い銏′亢椤﹀綊鏌涢埞鍨姕鐎垫澘瀚换婵囨償閵忕姴鍘為梻鍌欑劍鐎笛呮崲閸岀偛绠犵€广儱妫涢悵鍫曟煛閸ャ儱鐏柍閿嬪灴閺岀喓绮欓幐搴㈠闯缂備胶濮甸幐姝岀亙闂佺粯锚瀵爼宕悙鐢电＜闁稿本姘ㄥ瓭闂佸疇顫夐崹褰掑焵椤掑﹦绉甸柛瀣閸┾偓妞ゆ巻鍋撻柣鏍с偢閹繝顢曢敃鈧悙濠勬喐閺冨牜鏁佹俊銈呭暊閸嬫挾鎲撮崟顒€纰嶅┑鈽嗗亝缁诲牆顕ｆ繝姘伋鐎规洖娲﹀▓鏇㈡⒑闁偛鑻晶瀵糕偓瑙勬礈婢ф骞嗛弮鍫澪╅柨鏇楀亾濞寸姴銈稿铏规崉閵娿儲鐏佹繝娈垮枤閺佽鐣烽幋鐘亾閿濆骸鏋熼柣鎾存礃閵囧嫰骞囬崜浣瑰仹缂備胶濮甸悧鐘诲蓟閿濆鏅查柛婊€鑳堕崥瀣⒑閸濆嫮鐒跨紒鏌ョ畺楠炲棝寮崼婢囧箹濞ｎ剙鐏紒澶愭敱缁绘繈鎮介棃娑楃捕閻庢鍠栭悥濂稿极閸愵喖顫呴柕鍫濇噺閻庮剟姊洪崨濠傚Е闁哥姵鐗犻敐鐐哄川鐎涙鍘梺鍓插亝缁诲啴宕抽挊澹濆綊鎮℃惔顔煎壎濠殿喖锕ュ浠嬬嵁閹邦厽鍎熼柕蹇嬪焺濡差剟姊绘担渚劸闁挎洏鍊濋獮妤€顭ㄩ崨顓炵亰婵犵數濮电喊宥夊疾閹绘帩鐔嗛悹杞拌閸庢垿鏌涘Ο鍦煓婵﹥妞介幃鐑藉箥椤旇姤鍠栫紓鍌欐祰椤曆囧疮绾惧锛傞梻浣虹帛閸旀牠鎮鹃鍛洸婵犲﹤鐗婇悡蹇擃熆鐠鸿櫣澧曢柛鏂诲€濆顐﹀醇濠靛啯鏂€濡炪倖姊婚妴瀣啅閵夛负浜滄い鎾跺仜濡插鏌ｉ敐鍥у幋妤犵偞甯￠獮瀣敇閻斿嘲鍘炲┑锛勫亼閸婃牠宕归悡骞盯宕熼鍌ゆ锤婵°倧绲介崯顖炴偂閻斿吋鐓欓弶鍫濆⒔閸掓壆鎮鑸碘拺闁硅偐鍋涢埀顒佹礈閳ь剚鐭崡鍐差嚕鐠囨祴妲堥柕蹇曞Х椤旀帡姊洪懖鈹炬嫛闁告挻鐟︽穱濠冪鐎ｎ偀鎷虹紓浣割儓濞夋洜绮婚懠顒傜＜妞ゆ棁濮ゅ畷宀€鈧鍠涢褔鍩ユ径鎰潊闁冲搫鍊瑰▍鍥⒒娴ｇ懓顕滅紒璇插€歌灋婵炴垟鎳為崶顒€唯闁冲搫鍊甸幏鍝勨攽椤旂偓鍤€婵炲眰鍊濋崺鈧い鎺嶇贰濞堟粎鈧娲橀崹鍧楃嵁濮椻偓閹虫粓妫冨☉妯煎搸濠电姷鏁搁崑鐐哄垂椤栫偛鍨傞柛锔诲幘娑撳秹鏌ㄥ┑鍡橆棤缂佺娀绠栭弻娑㈠焺閸愮偓鐣肩紓浣哄Т閸熷潡婀侀梺缁樏幖顐ｇ閸︻厾纾奸弶鍫涘妽鐏忎即鏌熷畡鐗堝殗鐎规洦鍋婂畷鐔煎箣濞嗗繐濮庨梺瀹狀潐閸ㄥ潡銆佸▎鎾村殐闁宠　鍋撶紒顔炬暩缁辨挻鎷呴崫銉у姰婵＄偞娼欓幗婊堝箲閵忕姭鏀介悗锝庝簽閸婄偤姊洪懖鈺婄劸闁诲繑鑹鹃埢宥夊冀閵娧呯槇闂佹眹鍨藉褎绂掑鍕箚妞ゆ劧绲块幊鍛存煛娓氬洤娅嶉柛鈺嬬節瀹曘劑顢橀悪鈧Σ鐑芥⒒娴ｅ憡鍟炵紒瀣灴閹椽濡搁埡浣哄幈?- 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁绘劦鍓欓崝銈囩磽瀹ュ拑韬€殿喖顭烽弫鎰緞婵犲嫷鍚呴梻浣瑰缁诲倿骞夊☉銏犵缂備焦顭囬崢杈ㄧ節閻㈤潧孝闁稿﹤缍婂畷鎴﹀Ψ閳哄倻鍘搁柣蹇曞仩椤曆勬叏閸屾壕鍋撳▓鍨灍闁瑰憡濞婇獮鍐ㄢ枎瀵版繂婀遍埀顒婄秵娴滄瑦绔熼弴銏♀拺闁告稑锕︾紓姘舵煕鎼淬倖鐝紒瀣槸椤撳吋寰勭€ｎ剙骞愰柣搴＄畭閸庤鲸顨ラ幖浣哄祦婵°倕鎳忛悡鐔兼煙閹呮憼缂佲偓閸愵喗鐓忛柛銉戝喚浼冨Δ鐘靛仜濞差厼鐣峰鍕闁间粙鏀遍崹鍦閹惧瓨濯撮柟缁樺笂婢规洟姊绘笟鈧埀顒傚仜閼活垱鏅堕幍顔剧＜閺夊牄鍔屽ù顕€鏌熼鐣屾噰妞ゃ垺顨婇崺鈧い鎺戝缁€澶愭煏閸繃顥犵紒鐘荤畺閹绠涢弮鍌氣叧闂佺顑嗛幑鍥嵁閸ャ劍濯撮柛锔诲幘閳诲鈹戦悩鍨毄闁稿绋戣灒濠电姴鍟伴々鏌ユ煕椤愶絾绀€闁哄绶氶弻娑㈠焺閸愵亖濮囬梺缁樻尭閸熶即骞夌粙娆剧叆闁割偅绻勯ˇ顓炩攽閻愭潙鐏﹂柤褰掔畺瀹曠懓鈹戠€ｎ偆鍘搁梺鍛婂姂閸斿矂鍩€椤掑倹鏆€规洘鍨块獮妯肩磼濮楀棙顥堟繝鐢靛仦閸ㄥ爼鎮烽敃鍌氱獥闁归偊鍘剧粻楣冩煙鐎电浠﹂悘蹇ｅ幘缁辨帗寰勬繝鍕ㄥΔ鐘靛仜閸燁偊鎮鹃敓鐘茬闁惧浚鍋嗛埀顒€顭峰Λ鍛搭敃閵忊€愁槱闂佺懓鐨烽弲婊呯矉閹烘柡鍋撳☉娆樼劷缂佺娀绠栭幃妤€鈽夊▎妯煎姺闂佹椿鍘奸鍥╂閹烘鏁婇柤鎭掑劚绾炬娊鎮楀▓鍨灈妞ゎ厾鍏橀獮鍐閵堝棗浜楅柟鑹版彧缂嶄線鎮伴幘缁樷拺闁煎鍊曟牎婵炲瓨绮堢划娆忕暦濠靛棛鏆嗛柛鏇ㄥ墮娴滄姊虹紒妯荤叆闁汇劍绻堝銊︾鐎ｎ偆鍙嗗┑鐐村灦閿氭い蹇ｅ墯娣囧﹪鎮欓浣糕偓鎰版煛鐏炲墽娲撮柡浣瑰姌缁犳盯寮撮悩妯荤矒閹?
            Item {
                height: 64
                
                Rectangle {
                    anchors.fill: parent
                    color: "#121828"
                    border.color: "#2d3748"
                    border.width: 1
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 16
                        
                        // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁绘劦鍓欓崝銈囩磽瀹ュ拑韬€殿喖顭烽幃銏ゅ礂鐏忔牗瀚介梺璇查叄濞佳勭珶婵犲伣锝夘敊閸撗咃紲闂佺粯鍔﹂崜娆撳礉閵堝洨纾界€广儱鎷戦煬顒傗偓娈垮枛椤兘骞冮姀銈呯閻忓繑鐗楃€氫粙姊虹拠鏌ュ弰婵炰匠鍕彾濠电姴浼ｉ敐澶樻晩闁告挆鍜冪床闂備胶绮崝锕傚礈濞嗘挸绀夐柕鍫濇川绾剧晫鈧箍鍎遍幏鎴︾叕椤掑倵鍋撳▓鍨灈妞ゎ厾鍏橀獮鍐閵堝懐顦ч柣蹇撶箲閻楁鈧矮绮欏铏规嫚閸欏宕抽梺杞扮劍閹倿鍨鹃敃鍌氶唶闁绘柨澧庣粻姘箾鐎电孝妞ゆ垵妫濋崺娑㈠箣閻樼數锛滈柣搴秵閸嬫帡宕曢妷鈺傜厱閹艰揪绱曠粻鏌ユ煏閸パ冾伃妤犵偛顑呴埞鎴﹀箛椤撴稒鐎伴梺璇插椤旀牠宕板☉銏╂晪鐟滄棃宕洪妷锕€绶為柟閭﹀墻濞煎﹪姊虹紒妯曟垼銇愰崘顏嗙焾妞ゆ洍鍋撴慨濠勭帛缁楃喖鍩€椤掆偓椤洩顦归柍銉畵瀹曞ジ濡烽妷褝绱甸梻浣瑰劤濞存岸宕戦崱娑栤偓鍛存倻閼恒儳鍘撻梺鍛婄箓鐎氼參宕冲ú顏呯厓闂佸灝顑呴悘鎾煛瀹€鈧崰鏍箠閺嶎厼鐓涘ù锝夘棑閹规洖鈹戦悩娈挎毌闁逞屽墰閸嬨劑宕戦姀銈嗙厸閻忕偛澧介埊鏇犵磼缂佹绠炵€规洘甯掗埥澶娢熺憴鍕枙闂備浇顕х€涒晠顢欓弽顓炵獥婵炴垯鍩勯弫瀣喐閺冨牆鏄ラ柕澶涚畱缁剁偤鏌熼柇锕€澧绘繛鐓庯躬濮婃椽寮妷锔界彅闂佸摜鍣ラ崹鍫曞箖閻㈢鍗抽柕蹇婃閹锋椽姊洪悡搴綗闁稿﹥娲栭悺顓熺節閻㈤潧浠╂い鏇熺矌缁骞掑Δ鈧闂佸湱澧楀妯肩矆閸愨斂浜滈煫鍥ㄦ尰椤ユ瑧绱掑Δ鈧ˇ鎵崲濞戞埃鍋撻悽娈跨劸閻㈩垰鐖奸弻锝嗗箠闁告梹鍨垮畷鍝勨槈閵忕姷顓洪梺缁橆焽閺佹悂鏁嶅鍫熺厽閹兼惌鍨崇粔鐢告煕鐎ｎ偄娴€规洘娲熸俊鐑藉煛閸屾粌骞堥梺鐟板悑閻ｎ亪宕圭憴鍕弿鐎广儱顦伴悡娑氣偓鍏夊亾闁逞屽墴瀹曚即寮介鐐电暫闂佺粯鍨兼慨銈夊磻鐎ｎ喗鐓曟い鎰╁€曢弸鎴︽煕婵犲嫬鍘存慨濠勭帛閹峰懏顦版惔婵婎洬缂傚倷鐒﹁ぐ鍐焽閳ユ剚鍤曠紓浣姑欢鐐烘煙闁箑鍔﹂柨鏇炲€归悡娆撴煟濡も偓閻楀﹦娆㈤懠顒傜＜闁绘鍎ら悵顏嗙磼缂佹绠炴俊顐㈠暙閳藉鈻庤箛锝喰熷┑鐘垫暩閸嬫盯骞婃惔鈭舵椽鏁傞悾灞告敵婵犵數濮村ú锕傚磹闁垮浜滈煫鍥ㄦ尭椤忋倝鏌涚€ｎ偅宕岀€殿喕绮欓、姗€鎮欏▓鎸幮ラ梻鍌欒兌椤㈠﹪骞撻鍡欎笉闁瑰濮撮ˉ姘辨喐閻楀牆绗氶柣鎾寸洴閺屾盯濡烽姀鈩冪彅闂佸搫顑嗛崹鍧楀蓟濞戞ǚ鏋庨煫鍥ㄦ礈椤旀帡姊洪崫鍕拱缂佸鍨块崺鐐哄箣閿曗偓楠炪垺淇婇妶鍜冩婵″弶鍔欏缁樻媴婵劏鍋撻埀顒勬煕鐎ｎ偅灏棁澶愭煟濡儤鈻曢柛搴㈠姍閺岋綁骞樼捄鐑樼亪闂佸搫鐬奸崰鏍蓟閸ヮ剚鏅濋柍褜鍓氶弲鍫曨敍閻愬鍘撶紓鍌欑劍钃辩紒鈧€ｎ喗鐓涢悘鐐插⒔濞叉潙鈹戦垾宕囧煟鐎规洘鍎奸ˇ鎶芥煟鎼粹槅鐓兼慨濠呮閳ь剙婀辨刊顓烆焽閹扮増鐓曢柕濞垮劜閸嬨儵鏌曢崱鏇狀槮闁宠棄顦～婊堝幢濡搫鍘為梻鍌欒兌缁垶骞愰崫銉㈠亾濞戞帗娅婃い銏＄懃椤撳ジ宕堕埡鍐跨闯闂備胶顭堥張顒勬偡瑜忛幏瑙勫鐎涙鍘遍梺缁樏壕顓熸櫠閻㈠憡鐓冮柕澶樺灣閻ｇ敻鏌熼鐣岀煉闁诡喖澧芥禒锕傛偩鐏炶浜版繝鐢靛У椤旀牠宕板Δ鍛︽繛鎴欏灩绾惧鏌熼崜褏甯涢柣鎾寸懅缁辨帒鈽夊Ο鏄忕缂備讲妾ч崑鎾斥攽閻樻鏆柍褜鍓欑壕顓㈠春閿濆洠鍋撶憴鍕８闁告梹鍨块妴浣糕槈濡粎鍠愬顏堝级閹稿海鈧娊姊婚崒姘偓鎼佸磹閹间礁纾圭€瑰嫭鍣磋ぐ鎺戠倞妞ゆ帒顦伴弲顏堟偡濠婂嫭鐓ラ柣锝呭槻閳诲酣骞橀弶鎴滄偅闁诲骸鍘滈崑鎾绘煃瑜滈崜娑氬垝閸喓绡€闁搞儯鍔庨崢閬嶆⒑閺傘儲娅呴柛鐘冲哺閹偤鎳為妷锝勭盎闂佸啿鎼崐濠氬矗閳ь剟姊洪崫鍕槵闁逞屽墯閸撴岸宕ョ€ｎ喖绠圭紒顔煎帨閸嬫挸鐣烽崶璺烘櫍缂傚倸鍊搁崐鎼佸磹妞嬪孩濯奸柡灞诲劚閻ょ偓绻涢幋娆忕仾缂佺姵甯″缁樻媴閾忕懓绗￠梺鎸庣娣囧﹪顢涘鎹愬惈閻庤娲樺ú鐔肩嵁閸ヮ剚鍋嬮柛顐犲灩楠炴姊绘担绛嬫綈闁稿孩濞婇、姘额敇閵忕姷鐤囬梺鎼炲劘閸斿秹宕ｈ箛鎾斀闁绘ɑ褰冮弳鐐烘煏閸ャ劎绠栭柕鍥у婵偓闁宠棄鎳撻埀顒€鐏濋埞鎴﹀焺閸愵亝鎲欏銈忛檮閹告娊寮婚敓鐘插耿婵炲棗璀﹂敐鍥╃＜缂備焦顭囩粻鐐烘煕閳哄倻娲撮柛鈹惧亾濡炪倖甯掗崐宄扳柦椤忓牊鐓熼柨婵嗘噹椤ㄦ瑩鏌ｉ姀鐙€鐓兼慨濠勭帛缁楃喖鍩€椤掆偓椤洩顦查悡銈夋煏閸繃绀岄柛瀣尭椤繈顢橀悢鍝勫殥婵＄偑鍊栧ú鈺冪礊娓氣偓閵嗕礁顫濈捄铏瑰姦濡炪倖甯掔€氼剟寮伴妷鈺傜厓鐟滄粓宕滃璺何﹂柛鏇ㄥ灠缁犳娊鏌熺€涙绠ュù鐘哄亹缁辨挻鎷呴崫鍕戯綁鏌ｉ幙鍕瘈鐎殿喛顕ч埥澶愬閻樻牓鍔戦弻鐔衡偓娑欘焽缁犳牗绻濋埀顒勫箥椤斿墽锛?
                        Item {
                            Layout.fillWidth: true
                            
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: mainContent.breadcrumbText()
                                color: "#94a3b8"
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
            
            // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁绘劦鍓欓崝銈囩磽瀹ュ拑韬€殿喖顭烽弫鎰緞婵犲嫷鍚呴梻浣瑰缁诲倿骞夊☉銏犵缂備焦顭囬崢閬嶆⒑闂堟稓澧曢柟鍐查叄椤㈡棃顢橀姀锛勫幐闁诲繒鍋涙晶钘壝虹€涙﹩娈介柣鎰彧閼板潡鏌熷畷鍥р枅妞ゃ垺顨嗗鍕偓锝嗘尰缁挸顫忕紒妯诲閻熸瑥瀚禒鈺呮⒑閸涘﹥鐓ョ紒澶婄埣楠炴垿濮€閵堝懐顦ㄥ銈嗘煥濡插牓鏁冮崒娑氬幈闂佸搫娲㈤崝宀勬倶閻樼粯鐓曢柟鑸妼娴滄儳鈹戦敍鍕杭闁稿﹥鐗犲畷婵嬫晝閳ь剟鈥﹂崸妤€鐒垫い鎺嶇劍閸欏繘鎮峰▎蹇擃伌婵炲牊绮嶉幈銊︾節閸愨斂浠㈤梺鍝勮嫰閹虫﹢骞冨▎鎾村殤閻犺桨璀︽导鍐ㄢ攽閻橆偅濯伴柛鎰╁妷閹稿啰绱撴担浠嬪摵閻㈩垽绻濋獮鍐煛閸涱喗鍎銈嗗姂閸ㄥ銆傞弻銉︹拻濞达綁顥撴稉鑼磽瀹ュ嫮绐旂€殿喓鍔嶅蹇涘煛閸愵亜顦╅梻浣风串缁蹭粙鎯夋總鍛婂剹婵炲棙鎸婚崑锝夋煕閵夘垳鐣遍悗姘叄閺屾盯寮埀顒勫垂閸噮鍤曞┑鐘宠壘閸楁娊鏌ｉ弮鍫缂佹劗鍋涢埞鎴︽倷閺夋垹浠ч梺鎼炲妽濡炰粙銆侀幘鎰佸悑濠㈣泛顑呮禒鈺佲攽椤旂煫顏呮櫠閻ｅ瞼鐭撴い鏃囧Г閸欏繐鈹戦悩鎻掍簽闁绘捁鍋愰埀顒冾潐濞叉鏁幒妤€鐓濋幖娣妼缁狅絾銇勯幘璺烘櫩婵犲﹤鐗婇埛鎺懨归敐鍛暈閻犳劧绻濋弻宥呯暋閹殿喖鈪甸悗瑙勬礃閸ㄥ灝鐣烽崡鐐╂瀻闊洦鎸炬禍鐗堢節閻㈤潧浠滄俊顐ｇ懇瀹曟繂螖閸涱喖浠悷婊呭鐢鎮￠崘顏呭枑婵犲﹤鐗嗙粈鍫熺箾閸℃ɑ灏伴柛瀣ф櫊閺岋綁骞嬮敐鍡╂濡炪値鍋勯幊姗€寮婚妸銉㈡婵☆垯璀︽导鍐ㄢ攽閿涘嫯妾搁柛锝忕到椤繐煤椤忓嫬绐涙繝鐢靛Т鐎氀兾ｉ崼銉︹拺闁圭瀛╃壕鎼佹煕婵犲啰绠炵€规洘妞介崺鈧い鎺嶉檷娴滄粓鏌熼崫鍕棞濞存粍鍎抽埞鎴︽倷閻愬厜鍋撶€ｎ剚宕叉繝闈涙－閸ゆ洟鎮归崶銊с偞婵℃彃鐗撻弻宥夊垂濞戞瑦婢掗梺绋款儐閹瑰洭寮崘顔肩＜婵﹢纭搁崥鍛節閻㈤潧鈻堟繛浣冲洤绠犻柟鐗堟緲缁犳澘顭跨捄鍙峰牓寮ㄦ禒瀣厽闁归偊鍨伴惃娲煙閻ｅ苯啸缂佽鲸甯￠、娆撴嚃閳诡兙鍊濋弻娑㈠箳閹捐櫕璇炲Δ鐘靛仦椤洭骞忛悩缁樺殤闁肩鐏氶崯娲⒒閸屾瑨鍏岀紒顕呭灦瀵濡搁埡浣虹枀闂佹寧绋戠€氼喚绮堟繝鍥ㄧ厱闁靛鍠栨晶顖炴煟閹惧瓨绀嬮柡宀€鍠栭幃娆擃敆婢跺鏆ラ梻浣筋嚃閸ㄩ亶鎮烽埡鍛祦閻庯綆鍠楅崑鎰版煟閵忋埄鏆滅紒杈皺缁辨捇宕掑顑藉亾閻戣姤鈷旂€广儱顦崹鍌炴煟閻旂厧浜伴柛銈嗘礋閺屾洘绻涢悙顒佺彆闂佺粯鎸诲ú鐔煎蓟瀹ュ浼犻柛鏇ㄥ亐閸嬫挾绱掑Ο缁樼彿闂傚倸鐗婄粙鍫ュ绩娴犲鐓ユ繛鎴灻顏堟煛閸♀晛澧伴柍褜鍓濋～澶娒哄鍫濈獥闁哄稁鍘归埀顑跨閳藉螣闁垮娼旀繝鐢靛仜濡瑩宕濆Δ鍕╀汗闁糕剝绋掗埛鎺楁煕鐏炲墽鎳嗛柛蹇撶灱缁辨帡顢氶崨顓犱桓濡ょ姷鍋為崝娆忕暦椤愶箑唯闁靛繒濮虫竟鏇㈡⒑閸撹尙鍘涢柛瀣缁參骞掑Δ浣镐簵闂侀潧鐗嗗Λ搴㈢濠婂牊鐓欓柟顖嗗啳鍩為梺璇叉禋娴滎亪寮诲澶嬬叆閻庯綆浜炴导灞解攽椤旂》鏀绘俊鐐扮矙閵嗕線寮撮姀鐘栄囨煕濞戝崬澧伴柟瑙勬礋濮婄粯鎷呯粙鎸庡€┑锛勫仜濞尖€崇暦瑜版帗鐒肩€广儱鎳愰弻褔姊洪崜鎻掍簼婵炲弶鐗犻幆灞轿旈埀顒勨€︾捄銊﹀磯闁惧繒鎳撻。鍦磽娴ｉ潧濮€妞ゆ泦鍥ｂ偓鏃堝礃椤斿槈褔鏌涢埄鍐炬當妞ゎ偄娲铏瑰寲閺囩喐鐝旈柣搴㈠嚬閸撴瑥鐣甸崟顖涒拺鐟滅増甯楅敍鐔兼煟閹虹偟鐣电€规洘鍨垮畷鎺楁倷鐎电甯鹃梻浣稿閸嬪懐鎹㈠鍛傦綀銇愰幒鎾跺幗闂佽宕樺▔娑㈠几濞戙垺鐓涚€光偓鐎ｎ剛鐦堥悗瑙勬礃鐢帡鍩ユ径濠庢建闁糕剝顨嗛悿鍌炴⒒閸屾瑦绁版い顐㈩槼閵囨劙宕橀鑲╃枀闂佽法鍠撴慨瀛橆攰闂備礁鎲″ú锕傚垂娴煎瓨鍋傛繛鎴欏灪閻撴洟鎮橀悙鎻掆挃闁活厼锕﹂埀顒侇問閸犳捇宕濋幋婵愭綎婵炲樊浜滄导鐘绘煕閺囥劌澧柟鐑戒憾濮婃椽鎮滈埡鍌涚彟闂佹悶鍔岄悥鍏间繆鐎涙鐟归柍褜鍓欓锝夊箻椤旂⒈娼婇梺鏂ユ櫅閸燁垶鎮甸幎鑺モ拻濞达綀娅ｉ妴濠囨煕閹惧绠為柟顔惧厴椤㈡稑袙绾绉い銏☆殜瀹曟帒顭ㄩ崪鍐棷闂傚倷鑳堕…鍫ュ嫉椤掑嫭鍋￠柕濞炬櫅缁€鍌炴倶閻愮紟鎺楀绩閼恒儯浜滈柡鍐ㄥ€告禍楣冩煛閸♀晛澧撮柡宀€鍠栭、娆撳Ω閵夈儲鐏撻梺缁樻尪閸庣敻寮诲☉銏╂晝闁挎繂娲ㄩ悾濂告⒑濮瑰洤鈧洟骞婂Ο渚綎濡わ箒锟ユ禍褰掓煙閻戞ɑ鎯堢紒杈╂暩缁辨挻鎷呴幓鎺嶅濠电偠鎻紞鈧柛濠呮閳藉顫滈崱妯虹紦闂備線鈧偛鑻晶瀛橆殽閻愭彃鏆㈤柕鍥ㄥ姍楠炴帡骞嬮悩鍨闂傚倷绀佸﹢閬嶅磿閵堝鍨傞柣銏㈢《閳ь剚鐗楀鍕箾閻愵剚鏉搁梻浣虹帛閸旀浜稿▎鎰珷闁挎柨澧界壕濂告煟濡櫣锛嶅褜浜濋妵鍕敃閵忋垻顔掗梺鍦帶濠€閬嶅箟閹绢喖绀嬫い鎰╁€曢柊閬嶆⒒閸屾瑨鍏岄柛瀣ㄥ姂瀹曟洘娼忛埡渚囨濡炪倖鎸鹃崰鎾汇€呴悜鑺ョ厵闂傚倸顕崝宥夋煃闁垮鐏╃紒杈ㄥ笧閳ь剨缍嗛崑鎺楀磿閵夆晜鐓曢幖杈剧磿缁犲鏌＄仦璇插鐎殿喗娼欒灒閻炴稈鈧厖澹曢梺鍝勬川閸嬬喖寮抽妶澶嬬厱闁哄洢鍔屾晶鎵磼閳锯偓閸嬫捇姊绘担鍦菇闁搞劏妫勯…鍥槼闁绘娴风槐鎾存媴閸濆嫪澹曞┑鐘灪椤洨鍒掗弮鍥ヤ汗闁圭儤鍨跺Σ顒€鈹戦悙鏉戠仧闁搞劌婀辩划濠氭惞椤愶紕绠氶梺闈涚墕鐎氼垶宕楀畝鍕厱婵せ鍋撳ù婊冪埣瀵鏁愰崼銏㈡澑闂佸搫娲ㄩ崑妯煎垝閼哥數绡€闁冲皝鍋撻柛灞剧矌閻撴捇姊洪棃娑欘棞闁稿﹤鐏濋锝嗙節濮橆儵銊╂煥閺冣偓閸庢娊鐛崼銉︹拻濞达絿鎳撻婊呯磼鐠囨彃鈧瓕鐏嬪┑鐐叉閹稿憡顢婃繝鐢靛█濞佳囶敄閸℃稒鍋傛繛鎴欏灪閸婂爼鏌ｉ幇閭︽澓闁搞倖鐟ラ埞鎴︻敊閻撳簶鍋撴繝姘劦妞ゆ帊绶￠崯蹇涙煕閻樻剚娈旀い顓炴搐閳诲酣骞掗弬鎹愬焻闂傚倸鍊烽悞锕傚磿瀹曞洦宕叉慨妞诲亾闁糕斁鍋撳銈嗗笂閼冲爼濡撮幒鏃傜＜闁逞屽墴瀹曠喖顢涘☉鎺撳?- 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁绘劦鍓欓崝銈囩磽瀹ュ拑韬€殿喖顭烽弫鎰緞婵犲嫷鍚呴梻浣瑰缁诲倿骞夊☉銏犵缂備焦顭囬崢杈ㄧ節閻㈤潧孝闁稿﹤缍婂畷鎴﹀Ψ閳哄倻鍘搁柣蹇曞仩椤曆勬叏閸屾壕鍋撳▓鍨灍闁瑰憡濞婇獮鍐ㄢ枎瀵版繂婀遍埀顒婄秵娴滄瑦绔熼弴銏♀拺闁告稑锕︾紓姘舵煕鎼淬倖鐝紒瀣槸椤撳吋寰勭€ｎ剙骞愰柣搴＄畭閸庤鲸顨ラ幖浣哄祦婵°倕鎳忛悡鐔兼煙閹呮憼缂佲偓閸愵喗鐓忛柛銉戝喚浼冨Δ鐘靛仜濞差厼鐣峰鍕闁间粙鏀遍崹鍦閹惧瓨濯撮柟缁樺笂婢规洟姊绘笟鈧埀顒傚仜閼活垱鏅堕幍顔剧＜閺夊牄鍔屽ù顕€鏌熼鐣屾噰妞ゃ垺顨婇崺鈧い鎺戝缁€澶愭煏閸繃顥犵紒鐘荤畺閹绠涢弮鍌氣叧闂佺顑嗛幑鍥嵁閸ャ劍濯撮柛锔诲幘閳诲鈹戦悩鍨毄闁稿绋戣灒濠电姴鍟伴々鏌ユ煕椤愶絾绀€闁哄绶氶弻娑㈠焺閸愵亖濮囬梺缁樻尭閸熶即骞夌粙娆剧叆闁割偅绻勯ˇ顓炩攽閻愭潙鐏﹂柤褰掔畺瀹曠懓鈹戠€ｎ偆鍘搁梺鍛婂姂閸斿矂鍩€椤掑倹鏆€规洘鍨块獮妯肩磼濮楀棙顥堟繝鐢靛仦閸ㄥ爼鎮烽敃鍌氱獥闁归偊鍘剧粻楣冩煙鐎电浠﹂悘蹇ｅ幘缁辨帗寰勬繝鍕ㄥΔ鐘靛仜閸燁偊鎮鹃敓鐘茬闁惧浚鍋嗛埀顒€顭峰Λ鍛搭敃閵忊€愁槱闂佺懓鐨烽弲婊呯矉閹烘柡鍋撳☉娆樼劷缂佺娀绠栭幃妤€鈽夊▎妯煎姺闂佹椿鍘奸鍥╂閹烘鏁婇柤鎭掑劚绾炬娊鎮楀▓鍨灈妞ゎ厾鍏橀獮鍐閵堝棗浜楅柟鑹版彧缂嶄線鎮伴幘缁樷拺闁煎鍊曟牎婵炲瓨绮堢划娆忕暦濠靛棛鏆嗛柛鏇ㄥ墮娴滄姊虹紒妯荤叆闁汇劍绻堝銊︾鐎ｎ偆鍙嗗┑鐐村灦閿氭い蹇ｅ墯娣囧﹪鎮欓浣糕偓鎰版煛鐏炲墽娲撮柡浣瑰姌缁犳盯寮撮悩妯荤矒閹?
            Item {
                height: 180
                Layout.fillWidth: true
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 16
                    
                    Repeater {
                        model: mainContent.effectiveStatusCards
                        StatusSummaryCard {
                            id: statusSummaryCard
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            cardData: statusSummaryCard.modelData || ({})
                            fallbackAccentColor: "#3b82f6"
                        }
                    }
                }
            }

            Item {
                visible: false
            }

            // 濠电姷鏁告慨鐑藉极閸涘﹥鍙忛柣鎴ｆ閺嬩線鏌涘☉姗堟敾闁告瑥绻橀弻锝夊箣閿濆棭妫勯梺鍝勵儎缁舵岸寮诲☉妯锋婵鐗婇弫楣冩⒑閸涘﹦鎳冪紒缁橈耿瀵鏁愭径濠勵吅闂佹寧绻傚Λ顓炍涢崟顖涒拺闁告繂瀚烽崕搴ｇ磼閼搁潧鍝虹€殿喖顭烽幃銏ゅ礂鐏忔牗瀚介梺璇查叄濞佳勭珶婵犲伣锝夘敊閸撗咃紲闂佺粯鍔﹂崜娆撳礉閵堝棎浜滄い鎾跺Т閸樺鈧鍠栭…鐑藉极閹邦厼绶炲┑鐘插閺夊憡淇婇悙顏勨偓鏍暜婵犲洦鍊块柨鏇炲€哥壕鍧楁煙閸撗呭笡闁抽攱鍨块弻鐔兼嚃閳轰椒绮舵繝纰樷偓鐐藉仮闁哄本绋掔换婵嬪磼濞戞ü娣柣搴㈩問閸犳盯顢氳閸┿儲寰勯幇顒夋綂闂佸啿鎼崐鐟扳枍閸ヮ剚鈷掑ù锝囨嚀椤曟粎绱掔拠鎻掆偓姝岀亱濠电偞鍨熼幊鐐哄炊椤掆偓鍞悷婊冪箳婢规洟鎸婃竟婵嗙秺閺佹劙宕ㄩ钘夊壍闁诲繐绻愮换妯侯潖濞差亜宸濆┑鐘插閻ｉ攱绻濋悽闈涗粶闁挎洦浜幃浼搭敋閳ь剙鐣峰鈧、妯侯煥閳ь剙效濡ゅ懏鈷戦柛锔诲幖閸斿鏌涢妸銉хШ鐎规洘鍔欓幃娆撴倻濡桨鐢绘繝鐢靛Т閿曘倝宕弶璺ㄦ懃闂備焦宕樺畷鐢稿磻閻愬搫绠為柕濞垮労濞撳鎮归崶顏勭处濠㈣娲熷缁樻媴閸涘﹥鍎撻柣鐐村嚬閸嬪﹤鐣峰┑鍡欐殕闁告洦鍋嗛崢鎾⒑绾懏褰х紒鐘冲灴閻涱噣濮€閵堝棛鍘撻梺鍛婄箓鐎氼剟鍩€椤掍礁濮嶉柡浣稿€垮畷婊嗩槾闁挎稒绻冪换娑欐綇閸撗冨煂闂佸湱鈷堥崑濠囨倵闁垮绡€闁汇垽娼ф禒婊堟煙閸愯尙绠伴悡銈嗕繆椤栨繂浜归柣顓熷哺瀵爼宕煎☉妯侯瀷缂備讲妾ч崑鎾绘⒒娴ｈ鍋犻柛搴灦瀹曟繂鐣濋崟顐ゅ姦濡炪倖甯掗崐鎼佸储閹绢喗鐓涢悘鐐额嚙閳ь剚顨婂畷鐗堢節閸パ咁攨闂佺粯鍔曞Ο濠傤焽娴犲鈷掑ù锝囨櫕濮ｅ洭鏌涚仦鍓х煂闁绘稏鍨归埞鎴︻敊绾兘绶村┑鐐叉嫅缁插潡宕氶幒鎴旀瀻闊洤锕ラ悗濠氭⒑鐠団€崇€婚柛鎰电厛濡﹪姊婚崒姘偓椋庣矆娴ｉ潻鑰块梺顒€绉寸粻鐘绘煙閹规劗袦婵炲樊浜滃洿婵犮垼娉涢鍛婵傚憡鈷戠紓浣姑悘杈ㄤ繆椤愩垹顏柛搴亰閺岋綁鎮㈤崫銉х厐濡炪倧绠撳褔鎮鹃悜钘夌闁挎洍鍋撶紒鐘差煼閺岋繝宕掑Ο鍝勫濡炪値鍋勭粔鎾煘閹达附鍊烽柛娆忣槸濞堝苯鈹戦悙鍙夊櫤婵炶尙鍠庨悾鐤亹閹烘挸浠虹紓浣割儓濞夋洟宕㈠ú顏呭€垫鐐茬仢閸旀碍銇勯敂璇茬仸闁诡喗鐟︾换婵嬪礃閿旇法鐩庨梻浣烘嚀閹碱偆绮旈悜绛嬫晩濠㈣埖鍔栭悡娆撴煟閵堝骸鐏℃い蹇曞Х缁辨帗娼忛妸锕€闉嶉梺鐟板槻閹虫ê鐣峰鍫濈闁圭儤鍨甸ˉ搴ㄦ⒒娴ｇ瓔鍤欐繛瀵稿厴閹兘骞庨挊澶岊槰闂佸吋鍓氶崹顏堝磻閹捐閿ゆ俊銈勮兌閸樿棄鈹戞幊閸婃劙宕戦幘缁樼厽婵°倐鍋撻柨鏇ㄤ簻閻ｇ兘鏁愰崼銏㈡澑闂佸搫鍊告晶鐣岀不濮橆剦娓婚柕鍫濇婢ь剛绱掗鎯р枅闁诡喚鍋ら獮鍡氼槷闁衡偓閼恒儯浜滈柡鍐ㄥ€哥敮鍫曟煃瑜滈崜娆忣焽閿熺姷宓侀柟閭﹀幘缁♀偓闂佺鏈〃鍡涘棘閳ь剟姊虹拠鎻掑毐缂傚秴妫濆畷鎴﹀幢濞戞ê鍋嶅銈呯箰閹虫劗寮ч埀顒勬⒑濮瑰洤鐏叉繛浣冲嫮顩风憸鏃堝蓟濞戞埃鍋撻敐搴′簼閻忓浚鍙冮弻宥囨嫚閼碱儷褏鈧娲栧畷顒勫煡婢跺ň鏋庨柟瀛樼箓缁楁岸姊婚崒姘偓椋庣矆娓氣偓閹ê鈹戠€ｅ灚鏅為梺鍛婄☉閻°劑宕愰崼鏇熺厵缂備降鍨归弸鐔兼煟閹惧娲撮柟顔斤耿閹瑩骞撻幒鍡樺瘱闂備礁鐤囧Λ鍕囬悽绋胯摕鐎广儱顦扮€电姴顭块懜鐬垹鐟у┑锛勫亼閸娿倝宕戦崨顖氬灊闁规儳纾弳锕€鈹戦崒姘暈闁稿妫楅湁闁挎繂鐗滃鎰版煟閿旇姤鈷掔紒杈ㄦ崌瀹曟帒顫濋钘変壕闁归棿绀佺壕鐟邦渻鐎ｎ亝鎹ｉ柣顓炴閵嗘帒顫濋敐鍛闂備浇顕栭崰鎾诲磿闂堟稓鏆︽俊銈呮噺閸ゅ啴鏌嶉崫鍕舵敾闂佽￥鍊濆缁樻媴缁嬫妫岄梺绋款儏閹冲海鍙呴梺鍝勭▉閸樼晫鈧艾鎳橀弻锝夊棘閹稿孩鍠愮紓浣哄█缁犳牠寮婚悢琛″亾閻㈡鐒鹃柍褜鍓氶崹鐢革綖濠靛牊宕夐柛婵嗗濞堛倝姊洪悷鏉挎倯闁伙綆浜畷婵囨償閵娿儳鐓戦梺鐟板⒔缁垶鎮￠弴銏＄厪濠㈣埖绋撻崚鏉库攽閳ヨ櫕鍋ラ柡灞界Ч閹稿﹥寰勫Ο鎭嶃劑姊洪柅鐐茶嫰婢ь垱銇勯弮鈧悧鐘茬暦娴兼潙鍗抽柕蹇曞Х椤㈠懘姊虹憴鍕姸婵☆偄瀚划濠氬冀閵娿倗绠氶梺闈涚墕閹冲酣寮抽悙鐑樼厱闁绘ê寮堕ˉ銏ゆ煛鐏炲墽娲寸€殿喗鎸虫俊鎼佸Ψ閵夘喗浜ゅ┑锛勫亼閸娧囨嚈瑜版帒鐤鹃柣妯烘▕濞兼牗绻涘顔荤盎缂佺姴缍婇弻锝夊箛椤撶偟绁峰銈庡亜閹冲酣鍩為幋锔藉€风€瑰壊鍠楁晥闂備胶鍎甸弲鈺呭垂閸洜宓侀柛鎰ㄦ杺閸嬪懘鏌涢幇銊︽珔闁逞屽墮椤兘寮婚敃鈧灒濞撴凹鍨遍埢鍫ユ⒑閸濄儱浠滈柣鏍帶椤繐煤椤忓嫪绱堕梺鍛婃处閸撴瑩宕戝澶嬧拺闁告稑锕ラ悡銉х磼婢跺本鍤€妞ゆ洩缍佸濠氬Ψ閵夘喗缍傞梻浣瑰缁嬫垹鈧凹鍣ｉ、娆撳即閵忥紕鍘告繝銏ｆ硾閿曪附鏅堕弴鐑嗙唵鐟滃瞼鍒掑▎蹇曟殾婵﹩鍘奸閬嶆倵濞戞瑯鐒介柛妯兼暩缁辨帡鎮欓浣哄嚒閻庤娲﹂崜姘辩矉瀹ュ棗顕遍悗娑欘焽閸樿棄鈹戦悙鏉戠仴鐎规洦鍓欓埢宥咁吋閸ワ絽浜鹃悷娆忓缁€鈧梺闈涚墕閹测剝绌辨繝鍥ㄥ€婚柤鎭掑劜濞呭棝鏌ｉ悢鍝ユ噧閻庢凹鍓熼、鏃堟倻濡偐鐦堥梺姹囧灲濞佳勭閳哄懏鐓欐繛鑼额唺缁ㄧ晫鈧灚婢橀敃銉х矉閹烘柡鍋撻敐搴濈敖闁汇倕鎳樺铏规崉閵娿儲鐏佹繝娈垮枛椤曨厾鍒掔拠宸僵闁煎摜鏁搁崢鍗烆渻閵堝棗濮夊┑顔芥尦瀹曟繂顭ㄩ崼鐔哄幈闁诲函绲芥晶搴ｇ矓椤曗偓閺岋紕浠﹂崜褎鍒涙繝纰夌磿閸忔ɑ鎱ㄩ埀顒勬煃閳轰礁鏆熺憸鏉挎噹閳规垿鎮欏顔兼婵犳鍠氶崑鎾剁矉瀹ュ鏁嗛柛鏇ㄥ亞閸旓箑顪冮妶鍡楃瑨闁哥姵鑹鹃…鍥箛椤撶姷顔曢梺鍛婄懃椤﹂亶鎯岄幒鏂哄亾鐟欏嫭纾婚柛妤€鍟块锝嗙鐎ｅ灚鏅ｅ┑鐘欏嫬鍔ゅù婊勫劤闇夐柨婵嗙墕閳ь兛绮欏顕€宕煎┑鍡欑崺婵＄偑鍊栭幐鐐叏鐎涙ɑ鍙忛柨鏃€鍨濈换鍡涙煟閹板吀绨婚柍褜鍓氶悧婊堝极椤曗偓楠炴帡寮崫鍕濠殿喗顭囬崢褎鏅堕幍顔剧＜妞ゆ棁鍋愭晶锔锯偓瑙勬礀閵堝憡淇婇悜鑺ユ櫆闁诡垎鍐啈闂傚倸鍊风欢姘焽瑜忛幑銏ゅ箳閹炬潙寮块梺姹囧灮椤牓鎮块鈧弻锝夊箛椤旂厧濡洪梺?- 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁诡垎鍐ｆ寖闂佺娅曢幑鍥灳閺冨牆绀冩い蹇庣娴滈箖鏌ㄥ┑鍡欏嚬缂併劌銈搁弻鐔兼儌閸濄儳袦闂佸搫鐭夌紞渚€銆佸鈧幃娆撳箹椤撶噥妫ч梻鍌欑窔濞佳兾涘▎鎴炴殰闁圭儤顨愮紞鏍ㄧ節闂堟侗鍎愰柡鍛叀閺屾稑鈽夐崡鐐差潻濡炪們鍎查懝楣冨煘閹寸偛绠犻梺绋匡攻椤ㄥ棝骞堥妸鈺傚€婚柦妯侯槺閿涙稑鈹戦悙鏉戠亶闁瑰磭鍋ゅ畷鍫曨敆娴ｉ晲缂撶紓鍌欑椤戝棛鈧瑳鍥ㄥ€垫い鎺戝閳锋垿鏌ｉ悢鍛婄凡闁抽攱姊荤槐鎺楊敋閸涱厾浠搁悗瑙勬礃閸ㄥ潡鐛崶顒佸亱闁割偁鍨归獮鍫ユ⒒娴ｅ摜绉洪柛瀣躬瀹曞綊骞嶉绛嬫綗闂佹寧娲栭崐褰掓偂閻斿吋鐓忛煫鍥ь儏閻忣噣鎮介娑氣槈閼挎劙鏌涢妷鎴濈Х閸氼偊姊虹拠鈥虫灍闁荤啿鏅犻妴渚€寮崼婵堫槹闂侀潧顭堥崕鐗堢珶閺囥垺鈷戦柛婵嗗閳诲鏌涘Ο鍦煓鐎规洘鍨归埀顒婄秵閸嬪棛寮ч埀顒佺節閻㈤潧孝闁稿﹦绮弲鍫曞即閻樺灚锛忛梺鍛婃寙閸涱厾顐肩紓鍌欒兌缁垶鎯勯鐐靛祦閻庯綆浜栭弸搴ㄧ叓閸ャ劍纾婚柟顕嗙秮濮婄粯鎷呯憴鍕哗闂佸憡鏌ㄩ惌鍌氱暦閹版澘閿ゆ俊銈傚亾闁藉啰鍠愮换娑㈠箣閻戝棛鍔烽梺鍝勬濡繈寮诲鍫闂佸憡鎸鹃崰搴ㄦ偩閻戣棄绠ｆ繝闈涘暞椤秴鈹戦绛嬬劸濡炲瓨鎮傞弫宥堢疀濞戞瑥浠┑鐘诧工鐎氼參藟閸懇鍋撳▓鍨灕妞ゆ泦鍥х叀濠㈣埖鍔曢～鍛存煃閸濆嫬鈧懓鈻嶉崶顒佲拻濞达絿鎳撻婊呯磼鐠囨彃鈧悂婀侀梺绋跨灱閸嬫稑效閺屻儲鐓冮柕澶涢檮閻忛亶鏌￠埀顒佺鐎ｎ偆鍘介梺褰掑亰閸撴瑧鐥閺屾盯鏁愰崨顖溞ㄩ梺鍝勭灱閸犳牠骞栬ぐ鎺濇晝妞ゆ帒鍟犻崑鎾舵崉閵娿垹浜炬繛鍫濈仢閺嬶附銇勯弴鍡楁搐閻撯€愁熆閼搁潧濮囨い顐㈡嚇閺岋絽螣閼姐倕鈪抽梺鍝勬噽婵炩偓鐎殿喖顭锋俊鎼佸煛娴ｈ妫熼梻浣稿閻撳牓宕伴幒妤€绠伴柛鎰靛枟閳锋垶鎱ㄩ悷鐗堟悙闁逞屽墯濞叉粓鍩€椤掍胶顣叉繝銏★耿閿濈偠绠涢幘浣规そ椤㈡棃宕熼鍡欏€為梻鍌欑閹测€趁洪敃鍌氬偍闁绘劦鍓欓閬嶆煕閵夘喖澧柍閿嬪灴閺屾稑鈹戦崟顐㈠闁哄稄绻濆娲偡閻楀牆鏆堥梺绋块閸熷灝鐣甸崟顖涒拺闁革富鍘奸。鍏肩節閵忊槅鐒界紒顔碱煼閹煎綊顢曢敍鍕暰闂備線娼ч悧鍡涘磹閸涘﹦顩查柟顖嗗本瀵岄梺闈涚墕妤犲憡绂嶅鍫熺厵闁惧浚鍋撻懓璺ㄢ偓娈垮枛椤兘寮幇鏉垮窛闁稿本绮岄弸娑㈡煙閻撳海绉洪柟顖氬€垮畷顐﹀Ψ瑜滃Σ绋库攽閻樺灚鏆╅柛瀣洴閹椽濡歌閸ㄦ繈鏌ｅΟ铏癸紞妞も晜鐓￠弻锝夊箛椤掑倷绮靛銈庡亝濞茬喖寮婚悢鐓庣闁逛即娼у▓顓犵磼閻愵剙鍔ら柛姘儑閹广垹鈽夐姀鐘茶€垮┑鈽嗗灥濡椼劑宕氭繝鍥ㄢ拺闁告縿鍎辨禒婊呯磽瀹ュ拑宸ユい顐㈢箻閹煎綊宕烽鐘靛幆婵犵數鍋涘Λ娆撳磿閹惰棄绀堥梺顒€绉甸埛鎴︽煟閻斿搫顣奸柛鐔哄仱閺岀喖顢欓悡搴樺亾閸ф鍨傚Δ锝呭暞閺呮繈鏌涚仦鎯у摵闁轰焦绮岄埞鎴炲箠闁稿﹥鎸剧划鍫熸媴缁洘鐏佸┑鐘绘涧椤戝棝鍩涢幒妤佺厱閻忕偟鍋撻惃鎴濐熆瑜庣粙鎾舵閹烘柡鍋撻敐搴′簻闁诲繑鎸抽弻銊モ攽閸繀绮跺銈嗘尭閸氬顕ラ崟顓涘亾閿涘崬瀚鍦磽閸屾艾鈧悂宕愰悜鑺ュ€块柨鏇氱劍閹冲矂姊虹拠鑼婵炲瓨宀稿畷銏ゅ礈瑜庨～鏇㈡煙閻戞ɑ鈷掔痪鎯у悑娣囧﹪顢涘顓熷創濡炪値鍋呯敮鈥愁潖閾忕懓瀵查柡鍥╁枑閻濇牠姊虹粙娆惧剭闁稿﹥绻堥幃浼搭敊闁款垰浜鹃柨婵嗛閺嬬喖鏌￠崨顔剧煉闁哄矉绠戣灒濞撴凹鍨卞瓭闂備胶顭堥鍡涘箲閸ヮ剙绠栨繝濠傜墛閸ゅ秹鏌曟竟顖氬暙缁犺崵绱撻崒娆掑厡闁稿鎸搁…鍨熼搹瑙勬濡炪倖甯掔€氼剟宕掗妸鈺傜厵闂傚倸顕崝宥夋煟閹垮嫮绉柣鎿冨亰瀹曞爼濡搁敃鈧壕鎶芥⒑閸涘﹦鎳冮悗姘煎弮楠炲牓濡搁敂鍓х槇闂佸憡鍔忛弬鍌涚閵忋倖鍊甸悷娆忓绾炬悂鏌涢弬璺ㄐら柟骞垮灩閳规垹鈧綆浜為ˇ銊╂⒑閹稿海绠撴俊顐ｎ殜椤㈡棃骞栨担鍏夋嫼闁荤姴娲﹂悡锟狀敁濡ゅ懏鐓曟俊顖濆吹閻帞鈧娲橀崹鍧楃嵁濮椻偓瀹曟粍鎷呴悮瀵稿簥闂備浇顕ч崙鐣岀礊閸℃稑纾婚柟鐑橆殔绾惧潡鏌曟径娑橆洭缂佺娀绠栭弻娑滅疀濮橆兛姹楁繝鈷€灞奸偗闁哄苯绉堕幉鎾礋椤愩倓绱濋梻浣筋嚃閸犳帡寮插┑瀣劦妞ゆ巻鍋撴繝鈧潏銊﹀弿闁圭虎鍟熸径濞炬斀閻庯綆鍋勯埀顒傛暬閺岋綁鎮㈤崫鍕垫毉闂佸摜鍠撻崑鐔烘閹烘梹瀚氶柟缁樺笚濞堢粯绻濈喊澶岀？闁轰浇顕ч悾鐑芥偄绾拌鲸鏅┑顔斤供閸撴瑧绮婇鈧缁樻媴閾忕懓绗￠梺鍛婃⒐閻楃娀鐛崱娆庢勃閺夌偞瀵ч惄顖涗繆閻戣棄鐓涢柛灞绢殕鐎氬ジ姊婚崒娆戣窗闁稿妫濆畷鎴濃槈閵忊€虫濡炪倖鐗楃粙鎺戔枍閻樼偨浜滈柡鍐ㄥ€瑰▍鏇犵磼閻樻彃鈷旈柣銉邯椤㈡﹢濮€閳哄偆妫栫紓鍌欒濡狙囧磻閹剧粯鐓熼幖娣焺閸熷繘鏌涢悩宕囧⒌闁炽儻绠撻幃婊堟寠婢跺鈧剟鎮楅獮鍨姎妞わ富鍨崇划鍫⑩偓锝庡亖娴滄粓鐓崶銊﹀鞍闁革絿鎳撻埞鎴︻敍濞嗗繐绁┑顔硷功缁垶骞忛崨鏉戝窛濠电姴鍊瑰▓姗€姊绘担鍛婅础妞ゎ厼鐗嗚灋婵犻潧妫涢弳锕傛煟閺冨倵鎷￠柡浣稿暣閺屾洝绠涢敐鍡欑獢闂佹眹鍨归…宄邦潖婵犳艾纾兼慨姗嗗厴閸嬫捇骞栨担鍝ワ紮闂佺粯鍨兼慨銈夊吹閸曨垱鐓曢柟鎹愬皺閸斿秹鏌涜箛鏃傜煉闁哄本鐩、鏇㈡晲閸℃瑯妲梻浣圭湽閸婃宕戦幘璇参﹂柛鏇ㄥ枤閻も偓闂佸湱鍋撻幆灞轿涢敓鐘冲€甸悷娆忓缁€鈧悗娈垮枛婢у酣骞戦姀鐘斀閻庯綆鍋掑Λ鍐ㄢ攽閻愭潙鐏﹂柣鐕佸灠铻為柕鍫濐槹閳锋垿鏌涘┑鍡楊仾闁挎稒妫冨濠氬礋椤愩埄浼冨Δ鐘靛仜閻楁挻淇婇幖浣肝ㄦい鏃囨閻︽粓姊洪懡銈呅㈡繛娴嬫櫅椤曪綁骞樼€靛摜褰鹃梺鍝勬川閸嬫劙寮ㄦ禒瀣厽婵☆垱妞块崯蹇涙煛閸♀晛鐏﹂柡灞剧洴楠炴鈧潧鎽滈悿鍕旈悩闈涗粶闁哥喐娼欓悾鐑藉箳濡や礁鈧兘鏌涘▎蹇ｆШ闁逞屽墻閸撶喎顫忛搹鍦煓閻犳亽鍔庢导鍥⒑缁嬫鍎愮紒瀣尵閸掓帞鎷犲顔藉兊闁哄鐗勯崝宀勫几閹达附顥婃い鎰╁灪婢跺嫭绻涢崣澶岀煉闁诡垰鑻埢搴ㄥ箻鐎电骞堟俊鐐€栭崝妤佹叏閹绢喗鍊舵い鏂垮⒔濡垶鏌涘▎宥呭姢闁活厽鐟╅弻锛勪沪閸撗佲偓鎺懨归悪鍛暤妤犵偞鍨块獮鍥敆婢跺妫ㄩ梻鍌氬€风欢姘跺焵椤掑倸浠滈柤娲诲灡閺呭爼顢涢悙瀵稿幍濡炪倖姊婚崢褔鍩€椤掍焦鍊愰柟顕€绠栭幃婊堟寠婢跺瞼鏉告俊鐐€栫敮鎺楀磹閻熸壋鏋旀繝闈涱儐閳锋垿鏌ｉ悢鍛婄凡闁哄棝浜堕弻銊╁即濡　鍋撳Δ鍐╊潟闁绘劕顕弧鈧梺绋挎湰缁秴鈻撴ィ鍐┾拺闁告挻褰冩禍婵囩箾閸欏澧柛搴亰濮婄粯鎷呴悷閭﹀殝缂備浇顕ч崐濠氬焵椤掍礁鍤柛锝忕秮楠炲棗鐣濋崟顐ゎ唺濠德板€撶拋鏌ュ箯缂佹绠鹃弶鍫濆⒔閸掍即鏌熼懞銉х煂缂侇喗妫冮幃婊兾熼崷顓犵暰婵＄偑鍊栭崝鎴﹀垂濞差亜纾婚柕澶嗘櫆閻撴瑦銇勯弮鈧崕铏闁秵鐓涘ù锝囶焾閺嗭絿鈧娲樼敮鎺楋綖濠靛绠熼悗锝庡墮閼板潡姊婚崒姘偓宄懊归崶褉鏋栭柡鍥ュ灩缁愭鏌熼幆褏鎽犻柛娆忕箻閺岋綁濮€閻樺啿鏆堥梺绋款儏閸婂潡寮诲☉銏犵疀闂傚牊绋掗悘鍫㈢磽娴ｅ搫鐝￠柛銉ｅ妿閸樹粙姊鸿ぐ鎺戜喊闁告鏅槐鐐哄箣閻愵亙绨婚柟鍏肩暘閸ㄥ鎯岄幒妤佺厸閻忕偛澧介‖鑲╃磼閻樺磭娲存鐐达耿瀹曟粍鎷呯粙鍟冪喖姊婚崒娆戭槮闁圭⒈鍋婇幆灞惧緞鐏炴儳搴婂┑鐘绘涧濡盯寮抽敃鍌涒拺妞ゆ巻鍋撶紒瀣╃窔瀵偊宕堕浣哄幗濠碘槅鍨靛▍锝嗙濞戞瑧绠鹃柛顐ｇ缚閸嬨垺鎱ㄦ繝鍕笡闁瑰嘲鎳樺畷銊︾節閸愩劌澹嶉梻鍌欑劍濡炲潡宕㈡總鏉嗗洭宕￠悜鍡楁婵犵數濮村ú銈囧瑜版帗鐓熼柡鍐ㄦ处閵嗗啴鏌ら崜韫盎闁宠鍨块幃鈺呭垂椤愶絾鐦庨梻浣侯焾椤戝棛绮欓幋锝囦罕婵犵數鍋涘Λ娆撳箰閸濄儱顥氶柛褎顨嗛悡鏇㈡煙闁箑骞橀柕鍫熸尦閺岋紕鈧綆鍋呴埛鎰版煏閸パ冾伃妤犵偞锕㈠畷锟犳倷閸忓摜妫梻鍌欒兌椤牓鏁冮敃鍌氭瀬濠电姵鑹鹃拑鐔兼煥濠靛棭妲归柛瀣閺屾稑鈹戦崱妤婁紝闂佸搫妫崣鍐潖濞差亜浼犻柕澶堝劜閻忎線姊虹粙娆惧剳闁哥姵鎸稿嵄闁圭増婢樼粻鎶芥煛閸愶絽浜鹃柣搴㈢瀹€鎼佸蓟閻旇櫣纾兼俊顖濇閻熸彃鈹戦悙鑼ⅱ缂侇噮鍨抽幑銏犫攽閸繃鐎虫繝銏ｆ硾閻ジ鏁嶉悢鍏尖拺閺夌偟澧楃粊鐗堛亜閺囧棗娲﹂崑鈺呮煟閹达絽袚闁稿瀚惀顏堝箯瀹€鍕懙闂佸搫鎷嬮崜娆撳煘閹达富鏁婇柡鍌樺€涢埀顒冩硶閻ヮ亪顢橀悢绋库偓鎰偓瑙勬处閸ㄦ娊鍩€椤掑﹦绉甸柛鐘愁殜閸╂盯骞嬮敂鐣屽幘婵犳鍠楅崝鏇㈠焵椤掍焦绀嬮柣搴ㄦ敱缁绘繈鎮介棃娑楃捕闂佸綊鏀遍崹鍦閻愬瓨濯撮柛婵勫劜濞堥箖姊洪崫鍕偍闁搞劎鎳撳ú鍨攽閻橆喖鐏辨繛澶嬬洴椤㈡牠宕ㄩ弶鎴犲姦濡炪倖甯掔€氼厼鈽夎閺屽秶鎲撮崟顐や紝闂佽鍠掗弲娑㈡偩閻戣棄鐐婇柤绋跨仛椤旀劙姊婚崒娆戝妽濠电偛锕銊╂焼瀹ュ懎鐎梺绉嗗嫷娈旈柦鍐枑缁绘盯骞嬮悙鍐╄壘鍗遍柛顐ゅ枑閸欏繑淇婇妶鍌氫壕闂佺粯顨呴敃顏勵嚕椤愶絻鍋呴柛鎰ㄦ櫇閸樹粙妫呴銏″闁圭澧界划濠氬箮閼恒儳鍘告繛杈剧到婢瑰﹪鎮￠懖鈹惧亾濞堝灝鏋涙い顓犲厴瀹曟椽宕熼姘鳖槰闂佸疇妗ㄧ欢锟犲焵椤掍焦灏电紒杈ㄦ尰閹峰懘宕烽娑欘潔婵＄偑鍊栭崹鐢稿箠鎼淬劌鐓濋柟鐐た閺佸洭鏌ｉ幋婵囶棡閻庨潧鐭傚娲濞戞艾顣烘俊銈囧Т閹诧紕绮嬪鍜佺叆闁告侗鍨抽敍婊呯磽閸屾瑧鍔嶉柨姘攽椤旂⒈妯€闁哄本绋戦埢搴ょ疀閺傛寧顏犳俊鐐€ゆ禍婊堝疮鐎涙ü绻嗛柛顐ｆ礀瀹告繃銇勯弽銊х煠闁哥偛鐖煎铏规嫚閹绘帩鍔夐梺鍛婂灥缂嶅﹤鐣烽幎鑺ユ櫜濠㈣泛鐗冮崑鎾存媴缁洘鐎婚梺瑙勫劤绾绢參鎮块埀顒勬⒒娴ｈ櫣甯涙い銊ユ嚇閹囧礃椤旇偐顔嗛梺鎯х箰濠€杈╁娴犲鐓曢悘鐐插⒔閹冲懘鏌涢弬璺ㄐч柡灞诲妼椤繈顢栭懞銉︽澑婵＄偑鍊栧Λ渚€宕戦幇顓熸珷闁靛繈鍊栭悡娆撴煕濞嗗浚妲归柛濠冨姉閳ь剚顔栭崰鏍偉閻撳海鏆︾憸鐗堝俯閺佸﹪鏌ら幁鎺戝姶闁哥偛缍婂濠氬磼濞嗘垹鐛㈤梺閫炲苯澧紒瀣崌閸╃偤骞掑Δ浣哄幈闂佸搫鍟犻崑鎾绘煕閵娧勬毈妞ゃ垺宀搁弫鎰緞婵犲嫷鍟嬫俊鐐€栧濠氬煕閸繍鐒藉┑鐘叉处閳锋垿鏌ｉ幇顖涱棄闁告梹绮撻弻锛勨偓锝庝簵閸嬨垻鈧娲橀崹鍧楃嵁濡偐纾兼俊顖濇〃缂傛捇姊绘担鍛婂暈缂佸鍨块弫鍐Χ閸℃瑯娴勫銈嗘尵閸嬫劙寮ㄦ禒瀣厽闁归偊鍓欑痪褎銇勯妷锔剧煂缂佽鲸甯炵槐鎺懳熼崫鍕垫綒婵＄偑鍊戦崹鍝勎涢崘顔衡偓渚€寮崼婵嗚€垮┑鐐叉閸╁﹦妲愰弻銉︹拻濞撴埃鍋撻柍褜鍓涢崑娑㈡嚐椤栨稒娅犻悗鐢电《閸嬫挾鎲撮崟顒傤槰闂佹寧娲忛崹浠嬪Υ娴ｇ硶鏋庨柟鐐綑娴犲ジ鏌ｈ箛鏇炰哗婵☆偄瀚晥闁哄被鍎查埛鎺楁煕鐏炲墽鎳呮い锔肩畵閺岀喓鍠婇崡鐐扮盎闁绘挶鍊濋弻銊╁即閻愭祴鍋撹ぐ鎺戠柧妞ゅ繐鐗婇埛鎺懨归敐鍥╂憘闁搞倖鐟︾换娑氣偓娑欘焽椤ｈ尙绱掗崒姘毙㈡い顓滃姂瀹曞崬螖閸愩劉鍋撻幒妤佲拻闁稿本鐟ч崝宥夋煙椤旇偐鍩ｇ€规洘娲栭…銊╁礃閹勫€梻浣稿閸嬪懐鎹㈤崘顔肩？闁圭偓鏋奸弨浠嬪箳閹惰棄纾规俊銈勭劍閸欏繘鏌ｉ幋锝嗩棄缁炬儳顭烽弻锝夊箛椤旂厧濡洪梺鍝勬噺缁诲牓寮婚弴锛勭杸閻庯綆浜栭崑鎾诲箣閿曗偓閻掑灚銇勯幋锝嗙《妞わ讣绠撻弻宥囨嫚閺屻儱寮板Δ鐘靛仦閿曘垹顕ｉ崼鏇炵闁绘劕鐏氬▓鍫曟⒒閸屾艾鈧绮堟笟鈧畷顖炲锤濡も偓缁犺銇勯幇鍫曟闁哄懏绻堥弻鏇熷緞閸繂唯闂佸憡鐟︾€笛呮崲濠靛鍋ㄩ梻鍫熷垁閵夛负浜滈柨婵嗛閺嬨倖淇婇崣澶婂鐎殿喗鎸虫慨鈧柣妯碱暜缁遍亶姊绘担渚敯闁规椿浜炵划濠氬箣閻樺樊妫滈悷婊呭鐢鎮″☉姘ｅ亾楠炲灝鍔氬Δ鐘虫倐閻涱噣寮介鐔哄弮闂佸憡鍔︽禍婊堝几濞戙垺鐓涢悘鐐靛亾缁€鍐磼缂佹娲撮柟顔界懇椤㈡鎷呴崫鍕埌闂傚倷娴囧畷鐢稿窗閹扮増鍋￠弶鍫氭櫇娑撳秹鏌熼幑鎰靛殭缂佺姵鐗楁穱濠囧Χ閸涱喖娅ら梺鍝勬噺閻擄繝鐛弽顐㈠灊闁荤喐婢橀埛澶愭⒑鐠囪尙绠伴柣妤€锕ョ粚杈ㄧ節閸嬭姤妫冮崺鈧い鎺戝缁犵娀骞栧ǎ顒€濡奸柣鎾达耿閺岀喐娼忔ィ鍐╊€嶉梺绋款儐閸旀妲愰幘瀛樺闁兼祴鍓濋崹鍧楀蓟閵娾晩鏁囬柣鎴濇鐎靛矂姊洪棃娑氬婵☆偅顨婇幃姗€濡烽敂璺ㄧ畾濡炪倖鍔х徊璺ㄧ不閻愭惌娈介柣鎰絻閺嗭綁鏌℃担瑙勫磳闁诡喒鏅犲畷姗€鐓鐘垫晨闂傚倸鍊风粈渚€宕ョ€ｎ剛鐭堥柟缁㈠枛閻ょ偓绻涢幋鐐叉疇濞存粌缍婇弻娑㈠即閵娿儳浠梺鎶芥敱閸ㄥ潡寮婚悢鍏煎殐闁宠桨妞掔划鍫曟⒑閸涘﹥鈷愰柨鏇樺灲瀵鈽夐姀鐘愁棟闁荤姵浜介崜閬嶅Χ閸モ晝锛滃銈嗘閸嬫劖鏅堕敃鍌涚厱闁宠鍎虫禍鐐繆閻愵亜鈧牜鏁繝鍥ㄥ殑闁告挷鑳舵稉宥呂旈敐鍛殲闁稿﹦鏁婚弻锝夊閳藉棗鏅遍梺缁樺笧閸嬫捇濡甸崟顒佸劅闁靛繆鈧櫕娈紓鍌欐祰妞村摜鏁敓鐘茬畺闁冲搫鎳忛幆鐐淬亜閹扳晛鐏柨娑欐崌閺岋絾鎯旈姀鈺佹櫛闂佸摜濮甸惄顖炪€佸鎰佹▌闂佺硶鏂傞崕鎻掝嚗閸曨剛绠鹃柣鎰靛墰閳ь剙顭峰楦裤亹閹烘垳鍠婇梺鍛婃尰閻燂附绌辨繝鍥х闁兼亽鍎幏缁樼箾閹炬潙鐒归柛瀣尰缁绘稒鎷呴崘鍙夊櫤鐎规洘鐓￠弻鐔兼倻濡闉嶉梺鍛婄懃缁绘垿濡甸崟顖氱闁告鍋熸禒濂告⒑閸涘﹤鐏╁┑顔炬暩閹广垹鈽夐姀鐘茶€垮┑鈽嗗灥椤曆囨瀹ュ鈷戦柟鎯板Г閺侀亶鏌涢妸銉т虎妞ゎ偄绻戠换婵嗩潩椤掑嫬鏁归梻浣虹帛濡線濡撮埀顒€鈹戦鍏兼悙妞ゎ亜鍟存俊鍫曞礃閵娿儱顫氱紓鍌欑贰閸犳骞戦崶褜鍤曢悹鍥ㄧゴ濡插牊绻涢崱妯虹仼闁伙箑鐗撳铏圭矙閹稿孩鎷辩紓渚囧枛閻楁捇骞栫憴鍕劅闁宠棄妫欑€靛矂姊洪棃娑氬婵☆偅鐟х划姘辨崉閵娧咃紲闂佸湱绮敮鎺楀煕閺冨牊鐓欐鐐茬仢閻忊晠鏌嶇憴鍕伌鐎规洟浜堕崺锟犲磼濮橆剙甯繝纰夌磿閸嬫垿宕愰弽顓炲瀭闁绘梻鍘уΛ姗€鏌涘畝鈧崑娑氱矆婢舵劖鐓欓弶鍫濆⒔閻ｉ亶鏌﹂崘顏勬灈闁哄本娲樼换娑㈡倷椤掍胶褰呯紓鍌欑贰閸嬪嫮绮旇ぐ鎺戣摕鐎广儱鐗滃銊╂⒑閸涘﹥灏甸柛鐘查叄閹箖鎮滈挊澶婂祮闂侀潧绻掓慨鐑筋敊閸涘瓨鈷戝ù鍏肩懅閸掍即鏌ゅú璇茬仯缂侇喖锕弫鍐磼濞戞帗瀚奸梺鑽ゅ枑閻熴儳鈧凹鍓熻棢閻庯綆浜堕悢鍡欐喐鎼淬劊鈧啯绻濋崶褎妲┑鐐村灟閸ㄥ湱绮婚幎鑺ョ厵闁绘劘妫勬俊鑲╃磽瀹ュ拑韬€殿噮鍋婇獮鍥级鐠恒劌鈧偤姊洪崨濠冨矮缂佲偓娴ｈ鍙忛柛蹇涙？缁诲棝鏌ｉ幇鍏哥盎闁逞屽墯閸ㄥ灝鐣烽弴鐔哥秶闁冲搫鍊告惔濠傗攽椤斿浠滈柛瀣尵閳ь剚顔栭崰娑㈩敋瑜旈崺銉﹀緞婵犲孩鍍甸梺鎸庣箓閹冲秵绔熼弴銏♀拺闁圭娴风粻鎾剁磼閵娿劌浜归柤楦块哺缁轰粙宕ㄦ繝鍕箞闂備焦瀵х换鍌溾偓姘煎櫍閹苯鈻庨幘瀵稿幐閻庡厜鍋撻柍褜鍓熷畷浼村冀椤撶喎浠掑銈嗘磵閸嬫挾鈧娲栫紞濠囥€佸▎鎾崇煑闁靛绠戞禍婵囩節绾板纾块柛瀣灴瀹曟劙寮介鐐殿唶闂佸綊妫跨粈浣告纯缂傚倷绀侀鍫濃枖閺囥垹姹查柨鏇炲€归悡蹇撯攽閻愰潧浜炬繛鍛噽閻ヮ亪骞嗚閸嬨垽鏌＄仦璇插鐎殿噮鍓熷畷褰掝敊鐟欏嫬鐦遍梺鑽ゅ仦閻旑剟姊介崟顒傗攳濠电姴娲ゅ洿闂佹悶鍎洪悘婵嬪Ψ閳哄倻鍘遍柣搴秵閸撴瑦绂掗柆宥嗙厵妞ゆ柣鍔岄崥姗€寮崼婵嗙獩濡炪倖鎸炬刊顓㈠船閻㈠憡鈷掑ù锝呮啞閸熺偟绱掔€ｎ偄绗掓い顓炴搐閳诲酣骞樺畷鍥跺敼闂備胶鍘ч幗婊堝磻閿濆棛顩叉繝濠傜墛閻撴瑩鏌熺憴鍕缁绢厽鍎抽湁闁绘ê纾惌宀€绱掓潏銊ユ诞闁诡喒鏅犲Λ鍐ㄢ槈濡ゅ啯宕熼梻鍌欑閻ゅ洭锝炴径鎰瀭闁秆勵殔缁犳牠鏌嶉崫鍕櫣闁搞劌鍊归妵鍕箛閸撲胶蓱闂佷紮绠戦柊锝咁潖缂佹ɑ濯村〒姘煎灣閸旂顪冮妶鍡楃仴婵☆偅绋撻崚鎺戔枎閹惧磭顓洪梺鎸庣箓濞诧絿绱旈弴鐐╂斀闁绘ê鐏氶弳鈺呮煕鐎ｎ偆鈽夐摶鐐寸箾閸℃ɑ灏紒鐘卞嵆閺岀喖姊荤€靛壊妲梺鎼炲妼閸婂潡寮诲☉妯兼殕闁逞屽墴瀹曟垿鎮欓悜妯轰簵闂佸搫娲ㄩ崰鍡樼濠婂嫨浜滈柟鎹愭硾閺嬪酣鏌ｈ箛瀣姎闂囧绻濇繝鍌涘櫣濞寸媴绠撻弻鏇㈠炊瑜嶉顓㈡煕閳哄绡€鐎规洘甯掗…銊╁礃閵娧冩憢闂傚倸鍊风粈渚€骞栭锕€瀚夋い鎺戝閸庡孩銇勯弽顐户鐎规挷鐒︽穱濠囶敍濞嗘垹协闂佸搫鍟悧鍡涙倿閸偁浜滈柟鐑樺灥椤忊晝绱掗悩闈涒枅闁哄瞼鍠栭獮宥夘敊绾拌鲸姣夐梻浣告惈閹峰宕滃杈ㄥ床婵犻潧娲ㄧ弧鈧梺绋挎湰缁矂路閳ь剟姊绘担鐟扳枙闁衡偓闁秴鍨傞柛锔诲幗椤洟鏌熼悜妯荤厸闁稿鎹囬弫鎰償閳ヨ尙鐩庢俊鐐€栧ú妯煎垝瀹ュ洦宕叉繝闈涱儏閻掑灚銇勯幒鎴濐仼缂佺姰鍎查妵鍕棘閸喒鎸冮梺鎸庣⊕缁捇寮婚敐鍡樺劅妞ゆ牗绮庢牎闂備胶顭堥鍛偓姘嵆楠炲啫顫滈埀顒佷繆閻戣棄鐓涢柛灞诲€栭埛鏍р攽閻橆喖鐏辨繛澶嬬洴椤㈡牠宕橀埡鍐炬锤濠电姴锕ら悧濠囨偂濞戙垺鐓曢柟鎵虫櫅婵″灝顭胯閻╊垶寮婚敓鐘插耿婵☆垵鍋愰悡澶愭⒑閸濆嫭婀扮紒瀣崌閸┾偓妞ゆ帒锕︾粔鐢告煕鐎ｎ亝鍣藉ù婊勬倐椤㈡﹢濮€閿涘嫬甯楃紓鍌氬€烽悞锕佹懌閻庤娲栭惉濂稿焵椤掑喚娼愭繛鍙夌墱缁辩偞绻濋崶銉㈠亾娴ｇ硶鏋庨柟鐐綑娴犲ジ鏌ｈ箛鏇炰粶闁靛牊鎮傚畷鎴澪旈崨顔规嫽闂佺鏈懝楣冨焵椤掑嫷妫戞繛鍡愬灩椤繄鎹勯搹鐟板Е婵＄偑鍊栫敮鎺楀磹閸涘﹦顩锋繝濠傜墛閻撶姵绻涢懠棰濆殭闁诲骏绻濋弻锟犲川閺夎法鍘柣搴濈祷閸嬫劙鍩€椤掍胶鈯曟い顓炴喘瀹曘垽鏌嗗鍡忔嫼闂佸湱顭堝ù椋庣不閹剧粯鐓曢柣鏇氱娴滄壆鈧娲樼换鍌烇綖濠靛牊宕夐柧蹇氼嚃閺€銊╂⒒娴ｅ憡鍟炴い銊ョ墦瀹曟垿骞囬弶璺ㄥ摋婵炲濮撮鍡涙偂閻旈晲绻嗘い鏍ㄧ箖椤忕娀鏌ㄥ☉妯夹ｉ柕鍥у閺佹劙宕卞▎蹇撶哗闂備礁鎼懟顖滅矓瑜版帒钃熼柕濞р偓閸嬫捇鏁愭惔婵堟晼婵炲鍘ч惌鍌炲箖濡も偓閳绘捇宕归鐣屼邯闂備胶绮悧鏇㈠Χ閹间胶宓侀柛鎰╁妷閺嬪酣鏌熼幆褏锛嶅Δ鐘叉喘濮婅櫣鎲撮崟顒€鍓归梺鍛娒肩划娆掓＂闁诲函缍嗛崑鍌滄崲閸℃稒鐓熼柟鏉垮悁缁ㄥ鏌嶈閸庢悂宕ㄩ纰扁偓娑㈡⒑閹稿海绠撴繛灞傚妽缁嬪顓兼径瀣幍闂佺顫夐崝锕傚吹濞嗘挻鐓㈤柛鎰典簻閺嬫盯鏌＄仦璇插闁宠鍨垮畷閬嶅煛閸屽偊绠撳娲川婵犲倸顫呴梺绋款儐閹瑰洭寮婚敐澶嬪亜闁告縿鍎抽悡鈧俊鐐€栭崹鐢杆囬鐐村仼闁绘垼妫勭粻锝夋煥閺囨浜惧銈庡亜閹虫﹢寮诲澶嬪癄濠㈣泛顑愬Λ鈥愁渻閵堝棙鈷愭俊顐㈠暙椤繘鎼归崷顓狅紲濠碘槅鍨伴幖顐︼綖瀹ュ鍋℃繝濠傚缁舵煡鏌涢悢鍛婄稇妞ゆ洩缍佸畷妯好圭€ｎ偆鈧姊虹憴鍕姢闁宦板姂瀹曞灚寰勯幇顓涙嫼闂佸憡绻傜€氼噣鍩㈡径鎰厱婵☆垰婀遍惌娆撴煙椤曞棛绡€鐎殿喗鎸抽幃銏ゆ惞鐠団€虫櫗闂傚倷鑳堕幊鎾绘偤閵娧勫床闁稿瞼鍋為崵瀣偡濞嗗繐顏存繛鍫滅矙閺岋綁骞囬鐔峰壒濠电偛鐗婇敃銏ゅ蓟瀹ュ牜妾ㄩ梺鍛婃尰缁诲牓鏁愰悙鏉戠窞闁归偊鍓涢鍥⒑閸涘﹦缂氶柛搴㈠▕閹矂骞樼紒妯煎幍缂傚倷闄嶉崹褰掑几閹剧粯鐓冪憸婊堝礈濮橆儵娑㈠礃閵娧勬濠殿喗銇涢崑鎾淬亜閵忥紕鎳囩€规洖寮剁粩鐔碱敍濮橆厼顦╅梺缁樻尰濞茬喖寮诲澶婄厸濞达絿顭堢粻璇差渻閵堝懐绠伴柣妤€妫楁晥闁告瑥顦换鍡涙煏閸繃鍣洪柛锝嗘そ閺屽秹鏌ㄧ€ｎ亞浼屽┑顔硷工椤嘲鐣烽悡搴僵妞ゆ挾鍠撹ぐ鍥╃磽閸屾瑦顦烽柤鍐茬埣瀹曟顫滈埀顒€顕ｇ拠娴嬫婵炲棙鍔曢崝鍛存⒑闂堟稓澧曢柟铏姇琚欓柕蹇嬪灮绾捐棄銆掑顒佹悙闁哄绋掗妵鍕箣濠垫劖鈻堥梺缁樹緱閸ｏ綁鐛幒鎳虫棃鍩€椤掑嫬纭€闁规儼濮ら悡蹇撯攽閻愵亜鈧劙宕戦幘娣簻闁圭儤鍨甸埀顒冨吹婢?
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                Flickable {
                    id: mainFlickable
                    anchors.fill: parent
                    contentWidth: parent.width
                    contentHeight: contentColumn.height + 48 // 婵犵數濮烽弫鍛婃叏閻戣棄鏋侀柛娑橈攻閸欏繘鏌ｉ幋锝嗩棄闁哄绶氶弻娑樷槈濮楀牊鏁鹃梺鍛婄懃缁绘﹢寮婚敐澶婄闁挎繂妫Λ鍕⒑閸濆嫷鍎庣紒鑸靛哺瀵鈽夊Ο閿嬵潔濠殿喗顨呴悧濠囧极妤ｅ啯鈷戦柛娑橈功閹冲啰绱掔紒姗堣€跨€殿喖顭烽弫鎰緞婵犲嫷鍚呴梻浣瑰缁诲倸螞椤撶倣娑㈠礋椤栨稈鎷洪梺鍛婄箓鐎氱兘宕曟惔锝囩＜闁兼悂娼ч崫铏光偓娈垮枛椤兘骞冮姀銈呯閻忓繑鐗楃€氫粙姊虹拠鏌ュ弰婵炰匠鍕彾濠电姴浼ｉ敐澶樻晩闁告挆鍜冪床闂備胶绮崝锕傚礈濞嗘挸绀夐柕鍫濇缁犲墽鐥銏╂缂佲檧鍋撻柣搴㈩問閸犳牠鈥﹂悜钘夌畺闁靛繈鍨洪崑姗€鏌嶉妷銉ュ笭濠㈣娲熷鍝勑ч崶褏浠惧銈嗘⒐閻楃娀骞冮悙娣亝闁告劑鍔庨弻鍫ユ⒑閸涘﹦缂氶柛搴＄－婢规洟鎸婃竟婵嗙秺閺佹劙宕卞Δ鍐偡濠电偛鐡ㄧ划搴ㄥ磻閹捐埖宕叉繝闈涱儐閸嬨劑姊婚崼鐔峰瀬闁靛骏绱曠粻楣冩偣閸ュ洤鎳愰弳銈夋⒑鐠団€虫珯缂佺粯绻傞悾宄邦潨閳ь剟銆侀弮鍫濆窛妞ゆ挾鍠撻埀顒傚亾缁绘繈鎮介棃娴躲垽鏌ㄩ弴妯衡偓婵嬪箖瑜斿畷鍗炩枎閹寸姷鍔堕梻浣稿閸嬪棝宕伴幇鏉跨煑闊洦鏌х换鍡樸亜閺嶃劎鈽夋い鏇熺矌閻ヮ亪顢橀悢宄板Б濡炪倐鏅涢妶绋跨暦閵娧€鍋撳☉娅辨岸骞忓ú顏呪拺闁告稑锕﹂埥澶愭煥閺囨ê鍔滅€垫澘瀚板畷鐔碱敍濞戞艾骞堥梻渚€娼ч…鍫ュ磹閺囩偟鏆ら柛鈩冪憿閸嬫挾鎲撮崟顒傗敍缂備胶绮换鍫ユ偘椤旇姤鍎熼柕濠忕畱濞堢喖姊洪棃娑崇础闁告劑鍔庨悿鍕⒒閸屾艾鈧兘鎮為敃鍌氬嚑濠靛倻顭堥悿鐐箾閹存瑥鐏╃紒鎰殕閹便劌顫滈崱妤€骞嬮梺鍝勵儐濡啴寮婚敓鐘茬闁挎繂鎳嶆竟鏇㈡⒒娓氣偓濞佳兠洪妶鍥ｅ亾濮樼厧鐏ｇ紒顔款嚙閳藉濮€閳╁啯鐝抽梻浣告啞濞诧箓宕归悧鍫㈩浄闁规壆澧楅埛鎺懨归敐鍛暈闁诡垰鐗撻弻锟犲醇椤愩垹顫紓渚囧枟閻熲晠鐛€ｎ喗鏅濋柍褜鍓熼幃鈥斥攽鐎ｎ亞顔愬┑鐑囩秵閸撴瑩鍩€椤掍緡娈滈柟顔兼健閸┾偓妞ゆ帒瀚埛鎴︽煕濠靛棗顏€瑰憡绻傞埞鎴︻敍閿濆懎绫嶉悗瑙勬处閸ㄦ娊鍩€椤掑﹦绉甸柛鐘愁殜閸╂盯骞掗幊銊ョ秺閺佹劙宕ㄩ鍏兼畼闂備浇顕栭崹浼存偋閹捐钃熼柨婵嗩樈閺佸洭鏌ｉ幋婵囶棡濡ゆ梻绱撻崒娆愮グ濞存粠鍓熷畷鎴︽倷閻㈢數鐣堕梺缁樻⒒閸樠囨倿閸偁浜滈柟鐑樺灥椤忣亪鏌涚€ｎ倖鎴﹀Φ閸曨垰鍗虫俊銈傚亾濞存粓绠栭幃妤冩喆閸曨剛顦ラ梺缁樼墪閵堟悂濡存担鑲濇梹鎷呴崫銉х嵁闂佽鍑界紞鍡涘磻閸涘瓨鍋熸繝濠傚缁♀偓闂佹眹鍨藉褑鈪烽梻浣规偠閸斿酣宕㈣閸┿垽寮惔鎾搭潔闂侀潧绻嗛崜婵嗏枍閸ヮ剚鈷戦梻鍫熶緱濡叉挳鏌￠崨顏呮珚鐎殿噮鍋呯换婵嬪礋閵娿儰澹曞Δ鐘靛仜閻忔繈宕濆顓滀簻闁挎柨鐏濆畵鍡涙煕閳哄倻娲存鐐村浮楠炲﹪濡搁妷褏楔闂佽桨鐒﹂崝娆忕暦閵娾晩鏁婇柦妯侯槹閻庨箖姊婚崒娆戝妽閻庣瑳鍛煓闁瑰濮烽惌鎾绘煕閹捐尙鍔嶆い顐ｆ礋閺岀喖鎮滃鍡橆吂闂佽桨绀侀澶愬蓟濞戞ǚ妲堥柛妤冨仜缁犵粯绻涚€电袥闁哄懐濞€瀵槒顦归柟绛圭節婵″爼宕ㄩ渚囨綗闂傚倷绀侀幖顐﹀嫉椤掑嫭鍎庢い鏍仧瀹撲線鏌涢妷顔煎妞ゃ儱鐗婄换娑㈠箣閻愬灚鍣繛瀛樼矤娴滎亜顫忕紒妯诲闁兼亽鍎哄Λ鍕⒑閸濄儱鏋嶉柛妤€鍟块悾閿嬪閺夋垹顔掗柣鐘叉穿鐏忔瑩宕濋敃鈧—鍐Χ閸℃娼戦梺绋款儐閹稿墽妲愰幒妤佸亹闁肩⒈鍎疯閳ь剝顫夊ú妯好洪悢鐓庤摕闁糕剝顨忛崥瀣煕閳╁啨浠︾紓鍐╂礃缁绘繈鎮介棃娴躲垽鏌ㄩ弴妯衡偓婵嗙暦閺夋娼╅柤鎼佹涧閳ь剛鏁婚弻娑㈩敃閿濆棛顦ㄩ梺缁樻尭缁绘劙鍩為幋锔藉€烽梻鍫熺◥婢规洖鈹戦悙鍙夊櫣婵☆偅绻堝璇差吋婢跺鍙嗛柣搴秵娴滅偞瀵煎畝鍕拺闁告繂瀚﹢鎵磼鐎ｎ偄鐏撮柛鈹垮劜瀵板嫭绻濇惔銏犵紦闂備礁澹婇崑鍡涘窗閹版澘姹插ù鐓庣摠閳锋垿鎮归崶顏勭毢缂佺姵澹嗙槐鎺旂磼濡櫣鐟查梺鍝勬嚀濞夋稖鐏冮梺鍛婁緱閸橀箖鎮块崟顖涒拺闂傚牊渚楅悞楣冩煕鎼淬垻鎳囬柟顔哄灲閹煎綊宕烽鐘插箚闂傚倷绀佹竟濠囨偂閸儱闂い鏇楀亾闁诡喗顨婇幃婊堟嚍閵夈垺瀚藉┑鐐舵彧缁蹭粙骞夐敓鐘茬柈闁绘劗鍎ら崑銊︺亜閺嶃劋绶辨繛鍫燂耿閺屸剝绗熼崶褎鐝濋梺璇″枟閻熲晛鐣烽锕€绀嬫い鎾跺仜琚濇繝纰夌磿閸嬫垿宕愰弽顐ｆ殰闁圭儤顨嗛弲婵嬫煥閺傚灝鈷旈柣顓熸崌濮婂宕奸悢鐑╁亾娴犲鍋￠梺顓ㄥ閸欏棝姊洪崨濠傚Е濞存粍绮撻幃锟犳偄閸忕厧鈧敻鎮峰▎蹇擃仾缁剧偓鎮傞弻娑欐償閵忕姭鏋欓梺绯曟杹閸嬫挸顪冮妶鍡楃瑨闁挎洩濡囩划鏃堟偨閸涘﹦鍘遍梺缁樕戦崜姘枔濠婂牊鐓欐い鏇炴缁♀偓閻庢鍠楅幐鎶藉箖閵忋倖鎯為悹鍥ｂ偓铏珬闂傚倸鍊烽懗鍫曞箠閹剧粯鍋ら柕濞у嫬搴婇梺绋挎湰缁海绮堟繝鍋綊鏁愰崼鐕佷哗缂佺偓鍎抽妶鎼佸蓟閺囩喓绠鹃柛顭戝枛婵酣姊虹拠鈥虫灍闁搞劌娼″濠氬即閵忕姷鍊為悷婊冪Ч椤㈡棃顢橀悢缈犵盎闂侀潧绻嗛崜婵堢矆鐎ｎ亖鏀芥い鏃傛嚀娴滈箖姊绘担鑺ョ彧闁稿骸銈搁弫鎾诲Ψ閳轰礁鐎繛瀵稿Т椤戝懘鎷戦悢琛″亾楠炲灝鍔氭繛璇х畵瀹曚即宕卞☉娆戝幈闂佸搫娲㈤崝灞剧閻愯褰掓偂鎼达絿鍔┑顔硷龚濞咃綁宕犻弽顓炲嵆闁绘柨鎲＄欢顓㈡⒒娴ｄ警鐒鹃梺甯到椤洩顦归柟顕€绠栭幃婊堟寠婢跺矈鏀ㄩ梻浣虹帛閸旀洜绮旈棃娴虫盯宕橀妸褎娈惧┑鈽嗗灡椤戞瑥危閻撳寒娓婚柕鍫濆暙閻忣亪鏌ㄥ鑸电厓閻熸瑥瀚悘鎾煙椤旇娅呴柍缁樻崌楠炴劖鎯旈姀銏⌒掓繝鐢靛Х椤ｄ粙宕滃┑濞夸汗闁告劦鍠栫粻鐘充繆椤栨繃顏犻柡鍡畵閺岀喐娼忛崜褏鏆犻悗鐟版啞缁诲啴濡甸崟顖氬唨妞ゎ厽鍨堕悾鑸电箾鐎涙鐭婂褏鏅Σ鎰板箳閹宠櫕姊归幏鍛偘閳╁喚娼旈梻鍌欒兌椤㈠﹤鈻嶉弴銏犵婵炲棙鎸哥粻姘舵倶閻愭潙鍔ょ紒鍓佸仱閹﹢鎮欏▓璺ㄥ姼婵炲瓨绮庨崑鎾汇€冮妷鈺傚€烽柤纰卞厸閾忓酣姊洪崨濠冣拹缁炬澘绉规俊鐢稿礋椤栨稒娅嗛柣鐘叉穿鐏忔瑦绂掗幖浣光拺闁圭娴风粻姗€鏌涚€ｃ劌鈧洟鎮鹃悜钘壩╃憸澶愬磻閹炬枼妲堟繛鍡橆焽閸旂兘姊洪崨濠忎緵濞存粠浜滈～蹇撁洪鍕槶闂佸湱绮敮濠勮姳閼测晝纾藉ù锝囩摂閸ゆ瑩鏌涢悢渚婵☆偁鍨藉铏规兜閸涱喖娑ч梻鍌氬鐎氫即骞冮垾鏂ユ瀻闊浄绲剧€靛矂鏌ｆ惔顖滅У濞存粍绻堥獮鎴︽晲婢跺浠哄銈嗙墬缁嬫捇濡撮幒鏃傜＜閺夊牄鍔岀粭褏绱掓潏銊ョ瑨閾伙綁鏌涜箛鏇炲付闁搞劏椴告穱濠囧Χ閸ヮ灝銉╂煕鐎ｎ剙浠遍柍銉畵瀹曞爼濡歌瑜伴箖姊洪崫鍕窛濠殿噮鍙冨畷鎴﹀箻缂佹ɑ娅滈柟鐓庣摠缁诲啴宕板顑芥斀闁宠棄妫楁禍鐐烘煕閻樺啿濮夐柟骞垮灩閳规垹鈧綆鍋勬禒娲⒒閸屾氨澧涚紒瀣姉閸掓帡宕奸弴鐔叉嫼濠电偠灏濠勮姳閻戣姤鐓曟俊銈傚亾闁哥喎娼￠幃楣冩倻閽樺鐤€濡炪倖鍨兼慨銈夊棘閳ь剟姊绘担铏广€婃俊鐙欏洤鐤炬繛鎴炃氶弸鏃堟煏婵炵偓娅嗛柍閿嬪灩閹叉悂鎮ч崼婵堢懖闂佹寧绋撻崰鎾舵閹烘鐒垫い鎺嶇劍婵挳鏌ц箛鏇熷殌闁哄棙顨婇幃妤呯嵁閸喖濮庡銈忕細閸楁娊骞冮敓鐘插嵆闁绘梻绻濈花濠氭⒑鐟欏嫬顥愰柡鍛洴閸┾偓妞ゆ帊鐒︾粈瀣偓娈垮枟閹倸顕ｉ鈧畷濂告偄閸濆嫬绠炲┑鐘愁問閸犳濡靛☉銏犵；闁规崘绉悷閭︾叆闁糕剝顭囬妴鎰渻閵堝骸浜滅紒缁樺姉閸欏懎顪冮妶鍛閻庢凹浜炲Σ鎰板醇閺囩啿鎷洪梺鍛婄☉閿曘儲寰勯崟顖涚厱閻庯急鍐у闂傚倷鑳堕…鍫ヮ敄閸℃稑绠查柛銉墮缁犳澘鈹戦悩鎻掓殭缂佸墎鍋ら弻娑㈠焺閸愬墽鍔锋繛瀛樼矋閻熲晛顫忕紒妯诲闁告稑锕ラ崕鎾斥攽閻愯尙婀撮柛鏂跨Ч楠炲牓濡搁埡鍌滃弳闁诲函缍嗛崗姗€寮搁弽顓熷€垫鐐茬仢閸旀碍绻涢懠顒€鈻堢€规洘鍨块獮姗€鎳滈棃娑欑€梻浣告啞濞诧箓宕滃☉鈶哄洭濡烽妷銏℃杸闂佺粯鍔樼亸娆忥耿閹绢喗鐓曢柡鍌氱仢椤掋垻绱掗弮鍌氭灈鐎殿喗鎸虫慨鈧柣妯活問濡差垳绱撻崒娆戝妽妞ゃ劌顦々濂稿Ω瑜戞慨鍐测攽閻樺磭顣查柍閿嬪灴閺屾稑鈹戦崱妤婁患闂佸搫妫欓悷锔炬崲濞戙垹绾ч柟瀛樼妇閸嬫捇骞栨担娴嬪亾閿曞倸鐐婃い鎺嗗亾缂佺姵绋掗妵鍕箻鐠虹洅銉︺亜閺冣偓濞茬喎顫忓ú顏勫窛濠电姳鑳剁换渚€姊洪崨濠勬噧缂佺粯锕㈤幃锟狀敆閸曞灚鏅㈤梺鍛婃处閸撴盯宕㈤幖浣瑰€甸柛蹇擃槸娴滈箖姊洪柅鐐茶嫰婢у鈧娲戦崡鍐差嚕娴犲鏁囨繝褎鍎虫禍鎯归敐鍥┿€婇柡瀣叄閺岀喖骞嗚鐢姷绱掗崡鐐靛煟婵﹥妞藉畷銊︾節閸愵煈妲遍梻浣告啞椤牆鈻嶉敐鍛傦綁骞囬鑺ョ€婚梺鐟板⒔鐞涖儵骞忕紒妯肩閺夊牆澧介幃濂告煟閳╁啯顥堢€规洜鏁诲畷姗€顢欓悾灞藉箞闂備胶绮摫闁告挻鑹鹃埢鎾愁潨閳ь剟寮诲☉銏犵闁规儳纾悷銊╂⒑娴兼瑧绋荤紒璇茬墕閻ｅ嘲顫滈埀顒勩€侀弮鍫濈妞ゆ挾鍋熼崝鐢告⒒閸屾艾鈧悂宕愰幖浣哥９闁绘垼濮ら崐鍧楁煥閺囩偛鈧摜澹曠捄銊㈠亾鐟欏嫭绀€婵炶绠戦悾鐑藉蓟閵夛妇鍘甸梺缁樺灦閿氶柣蹇撶Ч閺岋紕鈧綆浜炴晥闂佸搫鏈ú婵堢不濞戞埃鍋撻敐搴濈敖濞寸姵鍎抽—?
                    clip: true
                    
                    ColumnLayout {
                        id: contentColumn
                        width: parent.width
                        spacing: 24
                        anchors.margins: 24
                        
                        // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁绘劦鍓欓崝銈囩磽瀹ュ拑韬€殿喖顭烽弫鎰緞婵犲嫷鍚呴梻浣瑰缁诲倿骞夊☉銏犵缂備焦顭囬崢杈ㄧ節閻㈤潧孝闁稿﹤缍婂畷鎴﹀Ψ閳哄倻鍘搁柣蹇曞仩椤曆勬叏閸屾壕鍋撳▓鍨灍闁瑰憡濞婇獮鍐ㄢ枎瀵版繂婀遍埀顒婄秵娴滄瑦绔熼弴銏♀拺闁告稑锕︾紓姘舵煕鎼淬倖鐝紒瀣槸椤撳吋寰勭€ｎ剙骞愰柣搴＄畭閸庤鲸顨ラ幖浣哄祦婵°倕鎳忛悡鐔兼煙閹呮憼缂佲偓閸愵喗鐓忛柛銉戝喚浼冨Δ鐘靛仜濞差厼鐣峰鍕闁间粙鏀遍崹鍦閹惧瓨濯撮柟缁樺笂婢规洟姊绘笟鈧埀顒傚仜閼活垱鏅堕幍顔剧＜閺夊牄鍔屽ù顕€鏌熼鐣屾噰妞ゃ垺顨婇崺鈧い鎺戝缁€澶愭煏閸繃顥犵紒鐘插⒔閻ヮ亪顢橀姀鈺傤棖缂備讲妾ч崑鎾绘煟鎼淬埄鍟忛柛鐘崇墵閳ワ箓鎮滈挊澶岀暫闂侀潧绻堥崐鏍箚閻愮儤鐓曢柨鏃囶嚙瀵法绱掗崡鐐茬骇缂佺粯绻勯崰濠偽熷ú缁樼秹闂備焦鎮堕崝鎴濐焽瑜旈崺銏狀吋婢跺娅滄繝銏ｅ煐钃遍柡鍛仱濮婅櫣绮欑捄銊т紘闂佺顑囬崑銈夊春濞戙垹绠ｉ柨鏃傛櫕閸樺崬鈹戦悩缁樻锭婵☆偅顨婇、鏃堫敂閸喓鍙嗗┑鐐村灦閻燂箓藟閸喐鍙忓┑鐘插亞閻撹偐鈧娲滈崰鏍€侀弴銏狀潊闁宠　鍋撶紒杈ㄦ⒐娣囧﹪濡堕崶顬儵鏌涚€ｎ偆娲撮柟顔ㄥ洤绠荤紓鍫㈠Х缁犳岸姊鸿ぐ鎺擄紵缂佲偓娴ｅ憡鏆滃┑锛勫亼閸婃牕螞娴ｈ倽娑㈠礋椤栨稑鍓ㄥ┑鐘诧工閻楀﹪鍩涢幋锔界厾濠殿喗鍔曢埀顒佹礀閻☆厽绻濋悽闈涗沪缂佷焦鎸冲鎻掆槈閵忊晜鏅梺鎸庣箓椤︻垳绮绘繝姘€甸梻鍫熺⊕閹插摜鎲告导瀵哥暫婵﹥妞藉畷顐﹀礋椤掆偓缁愭盯姊虹粙娆惧剱闁告梹鐟ラ锝夊Ω閿旂虎娴勯柣搴秵閸嬪棝宕㈤挊澶嗘斀闁宠棄妫楅悘鈩冧繆閻愬弶鍋ョ€规洦鍨电粻娑樷槈濞嗘垵骞愰梻浣虹《濡狙囧疾濞戙垺鍊堕柨鏇炲€归悡娑㈡倶閻愭彃鈷旀繛鎻掔摠椤ㄣ儵鎮欓幖顓犲姺闂侀€炲苯澧存繛浣冲洤绠烘繝濠傜墛閸嬵亪鏌涢弴銊モ偓鐘诲籍閸喎浜归梻鍌氱墛缁嬫劕鈻介鍫熲拺闁告稑锕ラ埛鎰版煟閻旀潙鍔﹂柟顔藉劤閻ｏ繝鏌囬敂钘夌紦闂備線鈧偛鑻晶瀛橆殽閻愯韬柟顔规櫅鐓ょ紓浣股戝▍灞解攽閻愯埖褰х紓宥佸亾濡炪倖娲橀悧鐘茬暦椤栨稒鍎熼柍閿亾闁衡偓娴犲鐓熸俊顖濐嚙婢ь垶鏌涢悢椋庣闁哄本鐩幃鈺呭箛娴ｅ湱鏉瑰┑鐘殿暜缁绘繂顭囪閸┿儲寰勯幇顒夋綂闂佺粯顭囬。顔炬闁秵鈷掑ù锝呮啞鐠愶繝鏌涚€ｎ偅宕岄柟顕€绠栭、鏃堝川椤栵絾閿ゆ繝鐢靛Т閿曘倝鎮ф繝鍥ㄥ亗闊洦绋撻崣鎾绘煕閵夛絽鍔氶柛鏂诲€楅惀顏堝箚瑜庨崑銉╂煛瀹€鈧崰鏍х暦濮椻偓瀹曪絾寰勬繝鍕春闂傚倷绶氶埀顒傚仜閼活垱鏅堕崣澶堜簻妞ゆ劑鍩勫Σ鎼佹偂閵堝棙鍙忔俊鐐额嚙娴滈箖姊洪棃娑欘棞闁哥喐娼欓悾閿嬬附缁嬫娼婇梺缁橆焾鐏忔瑦绂嶉悧鍫滅箚闁绘劦浜滈埀顒佺墵楠炴劖銈ｉ崘銊х崶闂佸綊鍋婇崢浠嬪矗韫囨梻绡€濠电姴鍊绘晶娑㈡煟閹惧鎳呴柍褜鍓涢幊鎾垛偓姘煎枟閺呰泛螖閳ь剟鏁冮姀銈嗗亱闁割偅绋愮花濠氭煙閸忚偐鏆橀柛鈺佸閳绘挸顭ㄩ崟顒€寮挎繝鐢靛Т閸嬪棝鎮￠懖鈹惧亾鐟欏嫭绀冮悽顖涘浮閿濈偛鈹戠€ｅ灚鏅為梺鑺ッˇ顔界珶閺囥垺鍊甸柣鐔告緲椤忣亪鏌涢敐鍥舵闁靛洦鍔欓獮鎺楀箣閻樻祴鍋撻悙宸富闁靛牆妫楃粭鎺撱亜閿斿灝宓嗙€殿喗鐓￠、鏃堝醇閻旇渹鐢绘繝鐢靛Т閿曘倝宕幍顔句笉缂備焦锕╁▓浠嬫煟閹邦厽缍戦柣蹇旀綑閳规垿顢欓悷棰佸闂傚倷绶氬褔鎮ч崱娑樼疇闁逛即鍋婇弫濠傗攽閻樻彃鈧敻寮ㄦ禒瀣闁规儼妫勭壕褰掓煙閻楀牊绶茬痪鎯ь煼閺岀喖骞嗚椤ｈ櫕淇婇顐㈢仸闁哄本鐩獮妯何旈埀顒勫箠鎼达絿绠旈柨娑樺绾捐棄霉閿濆牊顏犻悽顖涚洴閺岋綁鍩℃繝鍌滀桓闂佺粯渚楅崰姘跺焵椤掑﹦绉靛ù婊勭墵瀵憡鎯旈妸褍褰勯梺鎼炲劘閸斿秶澹曟繝姘厵妞ゆ洖妫涚粔顔芥叏婵犲啯銇濋柟绛圭節婵″爼宕ㄩ閿亾妤ｅ啯鈷戦柤濮愬€曢弸娆徝瑰搴″闁告帗甯楃换婵嗩潩椤掑偆鍟嬮梺鑽ゅТ濞测晝浜稿▎鎾村仼闁告繂瀚ч弨浠嬪箳閹惰棄纾归柡鍥ュ灩缁犵娀鐓崶銊р槈闁汇倝绠栭弻锛勪沪鐠囨彃濮庣紓浣哄Т濠€杈╂閹烘鏁婇柣鎰靛墮濞堝本绻濋埛鈧崟顓炲绩闂佸搫鐭夌紞鈧紒鐘崇洴楠炴鎹勭悰鈥冲緧濠电姵顔栭崰鏍晝閵夈儺娓诲ù鐘差儏缁犳牠鏌嶉埡浣告殲闁稿海鍠栭弻鏇＄疀婵炴儳浜鹃柧蹇ｅ亞閳ь剟绠栧濠氬磼濞嗘帒鍘″銈冨灩閿曘倝鍩㈠澶婂嵆闁靛繒濮烽澶愭⒑閹肩偛鍔撮柛鎾寸懇閹﹢骞庨懞銉у弳闂佸搫娲ㄩ崑娑㈠焵椤掍焦鍊愰柡灞诲姂婵″爼宕遍弴鐘电暰闂備線娼ч悧鍡欌偓姘煎櫍钘濋柨鏃傛櫕缁犻箖鏌涢銈呮灁缂佺姷鍋為幈銊︾節閸屻倗鍚嬮悗瑙勬礃鐢帡锝炲┑瀣闁绘垵妫欏鎴犵磽閸屾艾鈧绮堟笟鈧、鏍幢濞戞ê鐎梺绉嗗嫷娈曢柡鍛叀閺岋綁骞囬浣叉灆闂佸憡鑹鹃鍛粹€︾捄銊﹀磯濞撴凹鍨伴崜顒勬⒑閸︻厽鐒挎繛鍜冪悼濡叉劙骞樼拠鑼紲濠电偛妫欓崹鍨繆娴犲鐓㈤柛鎰靛枙閹查箖鏌熼绛嬬劸缂佺姵绋掗幆鏃堝灳瀹曞洢鍋栭梻鍌欑閹碱偊鎯屾径宀€绀婂〒姘ｅ亾鐎规洘妞介崺鍕礃椤忓棛妲囬梻浣侯焾閺堫剛鍒掗崼婵愭禆闁瑰墽绮悡鐔煎箹濞ｎ剙鐒洪柛鐔风箻閺屾盯鎮╁畷鍥ㄥ垱濡炪們鍨烘穱娲囪ぐ鎺撶厱闁崇懓鐏濋崝婊呪偓鍨緲鐎氫即鐛崶顒夋晢濠㈣泛顑囩粔閬嶆⒒閸屾瑨鍏岀紒顕呭灥閹筋偊鎮峰鍕凡闁哥喐澹嗛崚鎺撶節閸ヨ埖鏅梺閫炲苯澧い顐㈢箳缁辨帒螣閼测晜鍤岄梻渚€鈧偛鑻晶顔姐亜椤撶偞绌挎い锕€缍婇弻宥夊Ψ椤栨粎鏆ら悗瑙勬礀閵堝憡淇婇悜钘壩ㄧ憸婵堟閻㈠憡鈷掗柛灞剧懅鐠愪即鏌涢幘瀵告噮缂侇喗鐟╁畷鐔碱敇閻樻妲搁梻浣告惈缁夋煡宕濇繝鍐洸婵犲﹤鐗忛崣鎾绘煕閵夛絽濡介悘蹇涙涧椤儻顦抽柣鈺婂灦瀵鏁愭径濠勵啋闁荤姾娅ｉ崕銈夋倶閸儲鈷戠紓浣股戦幆鍕煕鐎ｎ亷宸ラ柣锝囧厴瀹曞ジ寮撮悙宥佹櫊閺屻劑寮村Δ鈧禍楣冩⒒閸屾凹妲哥紒澶婂濡叉劙骞樼€涙ê顎撶紓浣割儏缁ㄩ亶宕戦幘璇茬濞撴艾娲﹂弲鐐寸節閵忥絽鐓愰柛鏃€娲熼幏鎴︽偄閸濄儳顔曢梺鐟邦嚟閸婃垵顫濈捄鍝勫殤闁瑰吋鐣崝搴ㄥ矗閹剧粯鐓曢柕澶涚到婵′粙鎮樿箛鏇烆暭缂佺粯鐩畷濂告晲鎼粹剝顔勯梺鍓х帛閻楃娀寮婚敐鍜佹建闁逞屽墮椤洩顦崇紒鍌涘笒椤劑宕奸悢鍝勫箞闂備胶绮ú鎴犵矆娴ｅ湱顩叉い鏍仦閻撶喖鐓崶銊︾叆婵炴惌鍣ｉ弻鈥崇暆鐎ｎ剛袦闂佽鍠掗弲鐘茬暦濡ゅ懎宸濋柡澶庢硶閸旂敻姊婚崒娆戭槮闁硅绻濆濠氬Ω閳哄倻鍘愰梺鎸庣箓閹峰宕甸弴銏＄厱妞ゆ劧绲剧粈鈧紓浣哄Х閹虫捇婀侀梺鎸庣箓閹冲海鏁妸鈺傜厵闂佸灝顑嗛妵婵囨叏婵犲啯銇濈€规洜鍏橀、妯款槾闁告梻顭堥埞鎴︽倷鐎涙ê鍞夊銈冨劜閹搁箖宕氶幒鎾剁瘈婵﹩鍓涢鎺旂磽閸屾瑧鍔嶆い顓炴喘瀹曘垽鎮介崨濞炬嫼闂佽鍨庨崟顓ф炊闂備胶顭堥鍛偓姘煎弮楠炲牓濡搁埡渚€鍞堕梺闈涚箞閸ㄥ鏁嶅鍫熲拺缂佸瀵у﹢鎵磼鐎ｎ偄鐏ラ棁澶嬨亜閺冨倻鎽傛繛鍫滅矙閺岋綁骞囬澶婃婵犫拃灞藉⒋闁哄瞼鍠庨悾锟犳偋閸繃鐣婚梻浣筋嚃閸犳岸宕楀鈧悰顔碱吋婢跺﹦顦ㄩ梻濠庡亽閸樺ジ藟?
                        Item {
                            Layout.fillWidth: true
                            height: mainContent.isTradeRecordsView ? 560 : (mainContent.pageMode === "live-trading" ? 760 : 500)
                            
                            RowLayout {
                                anchors.fill: parent
                                spacing: 20
                                
                                // 濠电姷鏁告慨鐑藉极閸涘﹥鍙忛柣鎴ｆ閺嬩線鏌涘☉姗堟敾闁告瑥绻橀弻锝夊箣閿濆棭妫勯梺鍝勵儎缁舵岸寮诲☉妯锋婵鐗婇弫楣冩⒑閸涘﹦鎳冪紒缁橈耿瀵鏁愭径濠勵吅闂佹寧绻傚Λ顓炍涢崟顖涒拺闁告繂瀚烽崕搴ｇ磼閼搁潧鍝虹€殿喖顭烽幃銏ゅ礂鐏忔牗瀚介梺璇查叄濞佳勭珶婵犲伣锝夘敊閸撗咃紲闂佺粯鍔﹂崜娆撳礉閵堝棎浜滄い鎾跺Т閸樺鈧鍠栭…鐑藉极閹邦厼绶炲┑鐘插閺夊憡淇婇悙顏勨偓鏍暜婵犲洦鍊块柨鏇炲€哥壕鍧楁煙閸撗呭笡闁抽攱鍨块弻鐔兼嚃閳轰椒绮舵繝纰樷偓鐐藉仮闁哄本绋掔换婵嬪磼濞戞ü娣柣搴㈩問閸犳盯顢氳閸┿儲寰勯幇顒夋綂闂佸啿鎼崐鐟扳枍閸ヮ剚鈷掑ù锝囨嚀椤曟粎绱掔拠鎻掆偓姝岀亱濠电偞鍨熼幊鐐哄炊椤掆偓鍞悷婊冪箳婢规洟鎸婃竟婵嗙秺閺佹劙宕ㄩ钘夊壍闁诲繐绻愮换妯侯潖濞差亜宸濆┑鐘插閻ｉ攱绻濋悽闈涗粶闁挎洦浜幃浼搭敋閳ь剙鐣峰鈧、妯侯煥閳ь剙效濡ゅ懏鈷戦柛锔诲幖閸斿鏌涢妸銉хШ鐎规洘鍔欓幃娆撴倻濡桨鐢绘繝鐢靛Т閿曘倝宕弶璺ㄦ懃闂備焦宕樺畷鐢稿磻閻愬搫绠為柕濞垮労濞撳鎮归崶顏勭处濠㈣娲熷缁樻媴閸涘﹥鍎撻柣鐐村嚬閸嬪﹤鐣峰┑鍡欐殕闁告洦鍋嗛崢鎾⒑绾懏褰х紒鐘冲灴閻涱噣濮€閵堝棛鍘撻梺鍛婄箓鐎氼剟鍩€椤掍礁濮嶉柡浣稿€垮畷婊嗩槾闁挎稒绻冪换娑欐綇閸撗冨煂闂佸湱鈷堥崑濠囨倵闁垮绡€闁汇垽娼ф禒婊堟煙閸愯尙绠伴悡銈嗕繆椤栨繂浜归柣顓熷哺瀵爼宕煎☉妯侯瀷缂備讲妾ч崑鎾绘⒒娴ｈ鍋犻柛搴㈡そ閹ê鈽夊搴⑩枌闂備礁鎼張顒傜矙閹达腹鈧箓濡搁埡浣哄姦濡炪倖甯掗崐濠氭儗閸℃ぜ鈧帒顫濋敐鍛闁诲氦顫夊ú妯煎垝瀹€鍕仼闁绘垼妫勯悙濠囨煃閸濆嫬鏋︾紒鍗炲暱閳规垿鎮欓懜闈涙锭缂備浇灏慨銈囩博閻斿摜绡€闁搞儜鍜冪吹闂傚鍋勫ú锕€煤濠婂懎顥氶柤娴嬫櫇绾捐棄霉閿濆洦顏熼柛蹇ｅ墴閺屻倝骞忕仦鍓с€婇梺閫炲苯澧い鏃€鐗犲畷浼村冀椤撴稈鍋撻敃鍌涘€婚柦妯侯槹閻庮剟姊洪崷顓烆暭婵犮垺锕㈤悰顕€濮€閳ヨ尙绠氬銈嗙墬绾板秹骞嗛崼婵冩斀闂勫洤鈻嶉弴鐔衡攳濠电姴娲﹂崐閿嬨亜韫囨挸顏ら柛瀣崌瀵粙顢橀悢铚傜綍婵犲痉鏉库偓鏇㈠疮椤愶箑鍨傛繝闈涱儏缁犺绻涢敐搴″闁诲浚鍠栭…鑳檪缂佺粯绻傞～蹇旂節濮橆剛锛滃┑顔斤供閸忔﹢宕戦幘鎼Ч閹艰揪绲块悞鍏肩箾閹炬潙鐒归柛瀣尰椤ㄣ儵鎮欓幖顓熺暥濡炪値鍘归崝鎴濈暦閻旂⒈鏁嗛柍褜鍓欓锝夊锤濡や讲鎷哄┑顔炬嚀濞层倝鎮橀埡鍛厵閻犲泧鍛槇閻庤娲橀崹褰掑焵椤掑﹦绉甸柛鐘愁殜閹繝鎮㈤梹鎰畾濡炪倖鐗楁笟妤呭磿閵夆晜鐓曢柕鍫濇閹冲懐绱掓潏銊ユ诞闁诡喒鏅涢悾鐑藉炊瑜滃椋庣磽閸屾瑨鍏岀紒顕呭灦閹嫰顢涘杈ㄦ闂佸憡顨堥崐锝夋偄閻撳簼绱堕梺鍛婃礀閻忔氨绱炲鍥╃＝闁稿本鑹鹃埀顒勵棑閹峰骞撻幒婵堚偓鍓佹喐閺傝法鏆﹂柟杈剧畱缁犺崵绱撴担楠ㄦ岸骞忕紒妯肩閺夊牆澧界粔顒併亜椤愩埄妯€鐎规洘鍨块幃鈺冪磼濡厧骞堥梺鐟板悑閹矂宕伴弽顓熷€堕柨鏃堟暜閸嬫挾鎲撮崟顒傤槰缂備浇顕ч崯鎾嵁閸℃稑绫嶉柛顐ｆ儕閳哄懏鐓忓璺虹墕閸旀艾霉濠婂啰鍩ｆ慨濠傤煼瀹曟帒顫濋钘変壕闁归棿鐒﹂崑瀣攽閻樻彃鏆熼柣鐔活潐娣囧﹪濡堕崒姘闂備線娼уΛ娆戞暜濡ゅ啯宕叉繝闈涙－濞尖晜銇勯幘璺盒㈤悽顖炵畺濮婄粯鎷呯粙娆炬闂佺顑呴幊鎰板箲閵忋倕绀冩い鏂诲灩椤︾敻骞冮埡渚囧晠妞ゆ梹鍎抽弫鎼佹煟閻斿摜鐭嬬紒顔肩Ф缁顓奸崨顏呮杸闂佹悶鍎弬渚€寮ㄩ搹顐ょ瘈闁汇垽娼у瓭濠电偞娼欓崐鍨嚕椤愶箑绀冩い鏃傛櫕閸欏棗鈹戦悩缁樻锭婵☆偅顨婇、妤呭閵堝棛鍘介梺闈涚墕鐎氼喚绮ｉ弮鍫熺厵妞ゆ梻鐡斿▓姗€鏌熼崣澶嬪唉鐎规洜鍠栭、妤呭焵椤掑媻鍥煛閸涱喒鎷洪梺纭呭亹閸嬫稒鎱ㄩ埀顒€鈹戦悙宸Ч闁烩晩鍨堕妴浣割潩閼稿灚娅滄繝銏ｆ硾閿曨亪宕崼鏇熲拺闁告繂瀚婵嬫煕鐎ｎ剙鏋涙い銏∶叅妞ゅ繐鎳夐幏娲⒑閸涘﹦绠撻悗姘煎墴瀵櫕绻濋崶銊у幈闁诲函缍嗛崑鍛暦瀹€鍕厸?
                                ChartArea {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    marketData: mainContent.effectiveMarketData
                                    marketDataService: mainContent.marketDataService
                                    displayPositions: mainContent.effectivePositions
                                    currentSymbol: mainContent.liveOverviewChartSymbol
                                    currencySymbol: mainContent.displayCurrencySymbol
                                    autoWatchSymbols: false
                                    visible: !mainContent.isTradeRecordsView
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    visible: mainContent.isTradeRecordsView
                                    radius: 16
                                    color: "#121828"
                                    border.color: "#2d3748"
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 24
                                        spacing: 16

                                        Item {
                                            Layout.fillWidth: true
                                            height: 28

                                            RowLayout {
                                                anchors.fill: parent

                                                Text {
                                                    text: mainContent.liveText("tradeRecords")
                                                    color: "#f1f5f9"
                                                    font.pixelSize: 15
                                                    font.weight: Font.Medium
                                                }

                                                Rectangle {
                                                    radius: 8
                                                    color: mainContent.liveUnfinishedOrderCount > 0 ? "#163322" : "#1f2937"
                                                    border.color: mainContent.liveUnfinishedOrderCount > 0 ? "#22c55e" : "#475569"
                                                    border.width: 1
                                                    implicitWidth: tradeRecordBadgeText.implicitWidth + 14
                                                    implicitHeight: 24

                                                    Text {
                                                        id: tradeRecordBadgeText
                                                        anchors.centerIn: parent
                                                        text: mainContent.liveText("unfinishedOrders") + " " + String(mainContent.liveUnfinishedOrderCount)
                                                        color: mainContent.liveUnfinishedOrderCount > 0 ? "#bbf7d0" : "#cbd5e1"
                                                        font.pixelSize: 11
                                                        font.weight: Font.Medium
                                                    }
                                                }

                                                Item { Layout.fillWidth: true }

                                                Text {
                                                    text: "\u5171 " + String(mainContent.liveTradeRecordOrders.length) + " \u6761\u8bb0\u5f55"
                                                    color: "#94a3b8"
                                                    font.pixelSize: 12
                                                }
                                            }
                                        }

                                        Repeater {
                                            model: mainContent.liveTradeRecordOrders.slice(0, 8).map(function(order) {
                                                var orderData = order || ({})
                                                var unfinished = mainContent.isUnfinishedOrderStatus(orderData.status)
                                                return {
                                                    orderData: orderData,
                                                    unfinishedOrder: unfinished,
                                                    instrumentLabel: mainContent.marketInstrumentLabel(orderData.symbol || "--"),
                                                    sideText: mainContent.displayOrderSide(orderData.side),
                                                    sideColor: (orderData.side || "") === "BUY" ? "#ef4444" : "#10b981",
                                                    statusText: mainContent.displayOrderStatus(orderData.status),
                                                    statusColor: unfinished ? "#86efac" : "#93c5fd",
                                                    updatedText: orderData.updatedAt || orderData.filledAt || orderData.createdAt || "",
                                                    quantityText: orderData.quantity !== undefined ? (String(orderData.quantity) + "\u80a1") : "--",
                                                    priceText: orderData.price !== undefined ? ("\u4ef7\u683c " + mainContent.currencyText(orderData.price)) : "\u4ef7\u683c --",
                                                    progressText: unfinished ? "\u8fdb\u884c\u4e2d" : "\u5df2\u5b8c\u6210",
                                                    progressColor: unfinished ? "#22c55e" : "#64748b"
                                                }
                                            })

                                            Rectangle {
                                                id: orderRecordCard
                                                required property var modelData
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 72
                                                radius: 10
                                                readonly property var orderData: orderRecordCard.modelData.orderData || ({})
                                                readonly property bool unfinishedOrder: !!orderRecordCard.modelData.unfinishedOrder
                                                color: orderRecordCard.unfinishedOrder ? "#162132" : "#1a2235"
                                                border.color: orderRecordCard.unfinishedOrder ? "#22c55e55" : "transparent"
                                                border.width: orderRecordCard.unfinishedOrder ? 1 : 0

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 14
                                                    spacing: 6

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 10

                                                        Text {
                                                            text: orderRecordCard.modelData.instrumentLabel || "--"
                                                            color: "#f1f5f9"
                                                            font.pixelSize: 13
                                                            font.weight: Font.Medium
                                                            Layout.preferredWidth: 170
                                                            Layout.minimumWidth: 0
                                                            elide: Text.ElideRight
                                                        }

                                                        Text {
                                                            text: orderRecordCard.modelData.sideText || ""
                                                            color: orderRecordCard.modelData.sideColor || "#10b981"
                                                            font.pixelSize: 13
                                                        }

                                                        Text {
                                                            text: orderRecordCard.modelData.statusText || ""
                                                            color: orderRecordCard.modelData.statusColor || "#93c5fd"
                                                            font.pixelSize: 13
                                                            Layout.minimumWidth: 0
                                                            elide: Text.ElideRight
                                                        }

                                                        Item { Layout.fillWidth: true }

                                                        Text {
                                                            text: orderRecordCard.modelData.updatedText || ""
                                                            color: "#64748b"
                                                            font.pixelSize: 12
                                                            horizontalAlignment: Text.AlignRight
                                                            Layout.preferredWidth: 132
                                                            Layout.minimumWidth: 96
                                                            elide: Text.ElideLeft
                                                        }
                                                    }

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 10

                                                        Text {
                                                            text: orderRecordCard.modelData.quantityText || "--"
                                                            color: "#cbd5e1"
                                                            font.pixelSize: 12
                                                        }

                                                        Text {
                                                            text: orderRecordCard.modelData.priceText || "\u4ef7\u683c --"
                                                            color: "#f8fafc"
                                                            font.pixelSize: 12
                                                        }

                                                        Item { Layout.fillWidth: true }

                                                        Text {
                                                            text: orderRecordCard.modelData.progressText || ""
                                                            color: orderRecordCard.modelData.progressColor || "#64748b"
                                                            font.pixelSize: 12
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        Item { Layout.fillHeight: true }
                                    }
                                }
                                
                            }
                        }

                        // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁诡垎鍐ｆ寖闂佺娅曢幑鍥灳閺冨牆绀冩い蹇庣娴滈箖鏌ㄥ┑鍡欏嚬缂併劌銈搁弻鐔兼儌閸濄儳袦闂佸搫鐭夌紞渚€銆佸鈧幃娆撳箹椤撶噥妫ч梻鍌欑窔濞佳兾涘▎鎴炴殰闁圭儤顨愮紞鏍ㄧ節闂堟侗鍎愰柡鍛叀閺屾稑鈽夐崡鐐差潻濡炪們鍎查懝楣冨煘閹寸偛绠犻梺绋匡攻椤ㄥ棝骞堥妸鈺傚€婚柦妯侯槺閿涙盯姊虹紒妯哄闁稿簺鍊濆畷鎴犫偓锝庡枟閻撶喐淇婇婵嗗惞婵犫偓娴犲鐓冪憸婊堝礂濞戞碍顐芥慨姗嗗墻閸ゆ洟鏌℃径瀣劸婵炲皷鏅犲鍫曞醇濮橆厽娈ㄩ梺闈涚箞閸婃牠鍩涢幒鎳ㄥ綊鏁愰崼顐ｇ秷闂佺顑囨繛鈧柡灞剧⊕閹棃濮€鎺虫禒銏ゆ倵鐟欏嫭绀冪紒顔芥崌瀵偊宕橀鑲╋紲濠殿喗锕╅崢楣冨焵椤掑澧撮柟顔筋殜閻涱噣宕归鐓庮潛婵＄偑鍊х紓姘跺础閹惰棄绠栭柨鐔哄У閸嬪嫰鏌涜箛姘汗闁告鏁诲缁樼瑹閸パ冧紟缂備胶濮甸崹鍧楁偘椤斿槈鏃堝礃椤忓棴绱抽柣搴＄畭閸庡崬螞濡ゅ懏鍊堕柟缁㈠枟閻撴盯鎮橀悙鍨珪閸熺顪冮妵鍗炲€荤粣鏃堟煛鐏炲墽娲存鐐搭焽閳ь剟娼ч幗婊堟偪閸曨垱鍊甸悷娆忓缁€鍐╀繆閻愯埖顥夋い鏇稻缁傛帞鈧絽鐏氶弲婵嬫⒑閹稿海绠撻柟鍐叉捣閼洪亶濡烽敂鍓х槇闂佹眹鍨藉褔鍩㈤崼鐔虹濞达絽鍟垮ù鍌炲极閸曨垱鐓曢柍鈺佸暢濞夋彃顭块懜闈涘闂佸崬娲弻锝夊籍閸ヮ煈浠╁銈嗘⒐濞茬喎顫忓ú顏呭仭闂侇叏绠戝▓鍫曟⒑缁嬫鍎戦柛鐘崇墵閻涱喗绻濋崶褍宓嗛梺缁橆焽閺佹悂鏁嶅鍫熺厸濠㈣泛锕︽晶鎰版煛閸涱喚鈯曠紒鍌涘笚濞煎繘濡搁敃鈧?闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鏁愭径濠勵吅闂佹寧绻傞幉娑㈠箻缂佹鍘辨繝鐢靛Т閸婂綊宕戦妷鈺傜厸閻忕偠顕ф慨鍌溾偓娈垮枟閹告娊骞冨▎寰濆湱鈧綆浜欐竟鏇㈡偡濠婂懎顣奸悽顖涘笧婢规洟宕稿Δ浣哄幍闁诲海鏁搁…鍫熺墡闂備礁鎽滄慨鐢稿礉濞嗘挸钃熼柨婵嗘啒閺冨牆鐒垫い鎺戝閸嬪鏌涢埄鍐噮缂佺姵妫冮弻鐔兼倻濮楀棙鐣烽梺缁樻尰閻╊垶寮诲☉銏犲嵆婵°倓鐒﹂悵鏃堟⒑閸涘﹥顥栫紒鐘虫崌瀵鏁嶉崟顏呭媰闂佸憡鎸嗛崟顐㈢仭闂佽瀛╅鏍窗濡ゅ懎绠伴柧蹇ｅ亗缁诲棝鏌熼梻瀵割槮閸烆垶鎮峰鍐劯鐎规洦鍋勮灃闁告侗鍠掗幏缁樼箾鏉堝墽绉い顐㈩樀瀹曟垿鎮╃紒妯煎幈闂佸搫鍊藉▔鏇㈡倿閸濄儮鍋撶憴鍕闁告梹鐟╅悰顕€寮介妸锕€顎撻梺闈╁瘜閸橀箖寮抽姀銈嗏拻闁稿本鑹鹃埀顒傚厴閹虫宕奸弴妞诲亾閿曞倸閱囬柕蹇娾偓鍐茬哎闂備礁婀辨晶妤€顭垮Ο鍏煎枂闁挎洖鍊归崐鐢告煥濠靛棝顎楀褎澹嗙槐鎺楀焵椤掍胶绡€闁搞儯鍔庨崢鐢告煟鎼达絾鏆╂い顓炵墕閺嗏晜绻濈喊妯活潑闁稿甯″畷褰掑醇閺囩偠鎽曢梺缁樻⒒閳峰牓寮澶嬬厱闁斥晛鍠氬▓鏃€銇勮箛锝呭闁汇儺浜、妯衡槈濡懓顥氱紓鍌氬€搁崐鎼佸磹閹间礁纾圭€瑰嫭鍣磋ぐ鎺戠倞闁靛绲肩划鎾绘⒑閸涘﹦缂氶柛搴㈢叀瀵啿顭ㄩ崼鐔哄幗闂佸綊鍋婇崜姘跺箚閸儲鐓熼柟鐑樺灩娴犳盯鏌曢崶褍顏鐐村浮楠炲顢涘顒夋浆缂傚倸鍊风粈渚€藝闂堟侗鐒界憸鏂匡耿娴ｇ硶鏀介柣妯款嚋瀹搞儵鏌熼崘鎻掝劉缂佸倹甯掗…銊╁礋閳衡偓缁ㄨ顪冮妶鍡楀Ё缂佽尪娉曠划濠氭倷濞村鏂€闂佹枼鏅涢崯顖炲磹閹扮増鐓曢柍鍝勵儑缁♀偓閻庤娲樼敮鎺椻€﹂妸鈺佺闁靛ě灞芥暪闂傚倸鍊搁崐鎼佸磹妞嬪孩顐芥慨妯挎硾閻掑灚銇勯幒鎴濃偓鍛婄濠婂牊鐓犳繛鑼额嚙閻忥繝鏌￠崨顓犲煟妤犵偞锕㈤、娆撴偩鐏炶棄绠版繝鐢靛仩閹活亞寰婇崸妞烩偓锕傚醇閵夈儲杈堥梺鎸庣箓濞茬娀宕戦幘鑸靛枂闁告洦鍓涢ˇ銊╂煟閵忊晛鐏￠悽顖ょ節瀹曟椽濡烽敃鈧欢鐐测攽閻樻彃顒㈤柛宥夋涧椤啴濡堕崱妤€娼戦梺绋款儐閹稿墽妲愰幒鎳虫梹鎷呯粙鎸庢嚈婵犳鍠栭敃銉ヮ渻娴犲绠犻柨鐔哄Т鍥撮梺鍛婁緱閸犳岸鍩€椤掆偓婢х晫妲愰幘瀵哥懝闁搞儜鍕邯缂傚倷绀侀ˇ顖炴偉閻撳海鏆︽慨妯挎硾缁犺櫕淇婇妶鍌氫壕缂備胶濮惧畷鐢稿焵椤掑喚娼愭繛鍙夌墪鐓ら柨鏇楀亾闁崇粯鎹囬獮鍡氼槷闁衡偓娴犲鐓熸俊顖涱儥閸ゅ鈧鎮堕崕鐢稿蓟閿濆憘鏃堝焵椤掑嫭鏅濇い蹇撳閺嗭箓鏌熸潏鍓х暠缂佲偓鐎ｎ偁浜滈柡宥冨妿閵嗘帡鏌涘Ο鍦煓婵﹨娅ｉ幉鎾礋椤愩値妲版繝鐢靛仜閹冲繐煤閻旈鏆︽い鎺嶇缁剁偤鏌熼柇锕€骞橀柛妯兼暬濮婃椽宕楅梻纾嬪焻闂佺姘︾亸娆戝垝閸懇鍋撻敐搴′簼闁告瑥绻愰埞鎴︽偐閹绘帗娈查梺闈涙处缁诲嫰鍩€椤掍緡鍟忛柛锝庡櫍瀹曟粓鎮㈤梹鎰畾闂佺粯鍨兼慨銈夊疾濠婂牊鐓熼柨婵嗘嚀鐎氭壆绱掗煫顓犵煓婵﹤顭峰畷鎺戔枎閹搭厽袦闂備胶顢婃慨銈囧垝閹惧磭鏆﹂柟鎯版鍞梺瀹犳〃缁€渚€鎮楅銏♀拻濞撴艾娲ゆ晶顔剧磼婢跺鍤熺紒顔肩墦瀹曞崬顪冪紒妯绘澑闂備胶绮敋鐎殿喖鐖奸獮鏍箛椤斿墽锛滈梺缁橆焽閺佸摜鏁☉銏＄厪闁搞儜鍐句純閻庢鍣崳锝呯暦閹烘垟妲堟繛鍡樺灥濞懷囨⒒閸屾瑧顦﹂柟璇х節閺佸鎮楃憴鍕闁告挾鍠栧畷娲倷閸濆嫮顓洪梺鎸庢濡嫭绂嶉崷顓犵＝闁稿本鐟ч崝宥夋嫅闁秵鈷戞繛鍡樺劤楠炴牠鏌嶇憴鍕伌闁糕斂鍎靛畷鍗烆渻閸撗冨毈濠碉紕鍋戦崐鎴﹀礉瀹€鍕櫇妞ゅ繐鐗嗚繚婵炴挻鍩冮崑鎾垛偓娈垮櫘閸ｏ絽鐣烽悡搴樻斀闁割偒鍋勯弲顏勨攽閿涘嫬浜奸柛濠冪墪椤繑绻濆顒傛煣濡炪倖鍔х粻鎴﹀磼閵娾晜鐓欓梻鍌氼嚟閸斿秹鏌嶉柨瀣仼缂佽鲸甯￠、娑樷槈濞嗘埈妲版俊鐐€曟绋课涘┑瀣摕闁跨喓濮村婵囥亜閺冨牊鏆滄俊鎻掔墕椤啴濡堕崱妯尖敍缂傚倸绉崇欢姘跺Υ娴ｅ壊娼╅悹楦挎閸旓箑顪冮妶鍡楃瑨閻庢凹鍓熼幏鎴︽偄閸忚偐鍘介梺鍝勫暙閸婄敻骞忛敓鐘崇厸濞达綁娼婚崝鐔虹磼鏉堛劌绗掗柍钘夘槸椤粓宕卞Δ鈧竟澶愭⒒娴ｉ涓茬紒韫矙婵″墎绮欏▎鐐稁濠电偛妯婃禍婵嬪磻閿熺姵鐓涘璺哄绾泛螖閻樺弶鍠樻慨濠冩そ瀹曠兘顢樿閸旀悂姊洪崫銉ユ瀻婵炵》绻濋獮鍐┿偅閸愨晛鈧鏌ら幁鎺戝姎濞存粍绮庣槐鎺楁倷椤掆偓椤庢粌顪冪€涙ɑ鍊愮€殿喗鐓℃慨鈧柕鍫濇閹锋椽姊绘笟鍥т簽闁稿鐩幊鐔碱敍濞戞瑦鐝烽梺鍦檸閸犳鎮″▎鎰╀簻闁哄秲鍔嶉惃鎴︽煟閹烘垶鍟為柕鍥у婵偓闁炽儱鍟块幗鐢告⒑缁洘鏉归柛瀣尭椤啴濡堕崱妤€娼戦梺绋款儐閹稿濡甸崟顖ｆ晝闁靛繈鍨婚鍥煟閹惧崬鈧牠濡甸崟顔剧杸闁圭偓娼欏▍锝嗙箾鐎涙鐭嬫い銊ワ躬瀵鎮㈤崗鐓庢異闂佸啿鎼刊缁樺緞閹邦厾鍘遍梺瑙勫劤椤曨厾绮婚幘鑸靛仏婵炲棙鎸婚悡娆撴⒑椤撱劎鐣辨鐐搭焽缁辨帡鍩﹂埀顒勫磻閹剧粯鈷掑ù锝囶焾閺嗛亶鏌ｉ妸锔剧疄鐎规洘鍨块獮妯肩磼濡厧骞堥梻渚€鈧稑宓嗘繛浣冲啠鏋嶆慨妞诲亾闁哄苯绉烽¨渚€鏌涢幘鏉戝摵闁靛棗鍟换婵嬪磼濠婂嫭顔曢梻浣芥硶閸犳挻鎱ㄩ悽鍛婂€块柤娴嬫杹閸嬫捇鐛崹顔煎濠碘槅鍋呴惄顖氱暦閵忋倖鍋ㄩ柛娑樑堥幏铏圭磽閸屾瑧鍔嶉柨鏇楁櫅閳绘捇寮崼鐔哄幗濠德板€愰崑鎾绘煥閺囶亞鐣遍柍璇查叄婵偓闁靛牆妫楀▓銈咁渻閵堝棗绗掗柛濠傤煼閺佸秴顭ㄩ崘鐐瘜闂侀潧鐗嗗Λ妤佹叏閸岀偞鐓曢柕濞у懎绗￠梺鍝勬噷閸庨潧顫忕紒妯诲闁告稑锕ラ崕鎾绘⒑閸濆嫮澧遍柛鎾跺枛楠炲棝宕熼锝嗘櫖闂佺粯鍔︽禍鏍磻閹惧鐟归柍褜鍓欓锝夊箻椤旂⒈娼婇梺鏂ユ櫅閸燁垶鎮甸幎鑺モ拻濞达綀顫夐崑鐘绘煕鎼搭喖娅嶇€殿喗褰冮埞鎴犫偓锝庝簼鏉堝牆顪冮妶鍡樺暗闁哥姴绉堕幑銏ゅ幢濞戞瑧鍘介梺瑙勬緲閸氣偓缂併劌顭烽弻宥堫檨闁告挻宀搁幃褔鎮╅懡銈呯ウ闂佸綊鍋婇崰妤冣偓姘皑閳ь剛鎳撴竟濠囧窗濡ゅ啠鍋撻棃娑氱劯婵﹥妞介幊鐐哄Ψ閸愬彞閭挊婵嬫煢濡警妯堥柣鎺嶇矙閺屸€愁吋鎼粹€崇闂佺粯鎼紞渚€鎮￠锕€鐐婇柕濠忕畱闂夊秹鏌ｉ悩鍐插Ё缂佽鲸娲熼幆鈧い蹇撶墕缁犳氨鎲稿澶婄畺闁瑰瓨鍔叉惔銊ョ倞鐟滄繈鐓浣典簻闁靛骏绱曢埥澶嬨亜椤愶絿绠炴い銏★耿閹垹鐣￠弶娆炬濠电姷顣槐鏇㈠磻閹达箑纾归柕鍫濐槸绾惧鏌涘☉鍗炵仭闁哄棙绮撻弻鐔兼倻濮楀棙鐣堕梺缁樻尰閿曘垽寮婚悢鍛婄秶濡わ絽鍟宥夋⒑缁嬪尅鍔熼柛蹇旓耿瀵鍩勯崘銊х獮婵犵數濮寸€氼亪鎼规惔銊︹拺婵懓娲ゆ俊鍧楁煕閻樺磭澧い?
                        Item {
                            Layout.fillWidth: true
                            height: mainContent.pageMode === "live-trading" ? 304 : 220
                            visible: !mainContent.isTradeRecordsView
                            
                            MarketGrid {
                                anchors.fill: parent
                                marketData: mainContent.effectiveMarketData
                                marketSections: mainContent.liveMarketSections
                                visible: !mainContent.isPerformanceAnalysisView
                            }

                            Rectangle {
                                anchors.fill: parent
                                visible: mainContent.isPerformanceAnalysisView
                                radius: 16
                                color: "#121828"
                                border.color: "#2d3748"
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 24
                                    spacing: 16

                                    Repeater {
                                        model: mainContent.performanceCards

                                        Rectangle {
                                            id: performanceMetricCard
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            radius: 12
                                            color: "#1a2235"

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 18
                                                spacing: 10

                                                Text {
                                                    text: performanceMetricCard.modelData.title || ""
                                                    color: "#94a3b8"
                                                    font.pixelSize: 13
                                                }

                                                Text {
                                                    text: performanceMetricCard.modelData.value || ""
                                                    color: "#f8fafc"
                                                    font.pixelSize: 22
                                                    font.weight: Font.Medium
                                                }

                                                Text {
                                                    text: performanceMetricCard.modelData.detail || ""
                                                    color: "#64748b"
                                                    font.pixelSize: 12
                                                    wrapMode: Text.WordWrap
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        
                        // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁诡垎鍐ｆ寖闂佺娅曢幑鍥灳閺冨牆绀冩い蹇庣娴滈箖鏌ㄥ┑鍡欏嚬缂併劌銈搁弻鐔兼儌閸濄儳袦闂佸搫鐭夌紞渚€銆佸鈧幃娆撳箹椤撶噥妫ч梻鍌氬€稿ú銈壦囬悽绋胯摕闁靛鍎弨浠嬫煕閳╁啰鎳冩い銈傚亾濠碉紕鍋戦崐鏇犳崲閹烘挻鍙忓瀣椤洟鏌熼悜姗嗘當闁绘帒鐏氶妵鍕箳瀹ュ牆鍘＄紓浣叉閸嬫捇姊绘担渚劸闁哄牜鍓熼妴鍐幢濞戞ɑ顥濋梺閫炲苯澧存慨濠勭帛閹峰懘鎼归悷鎵偧闂備焦瀵у銊╁焵椤掍緡鍟忛柛鐘崇墵閹儲绺介崫銉ョウ闂佸搫绋侀崢鑲╃不濞戙垺鐓熸俊銈傚亾闁绘绻樺畷銏＄鐎ｎ偀鎷洪梻鍌氱墛缁嬫挻鏅堕弴鐔翠簻闁挎洖鍊烽幉楣冩煙椤旇棄鍔ら悡銈嗐亜韫囨挻鍣介柛妯绘倐閹宕楁径濠佸闂備線鈧偛鑻晶瀵糕偓瑙勬磻閸楁娊鐛鈧幊婊冣枔閹稿海绋愰梻鍌欑濠€閬嶅磿閵堝鍚归柨鏇炲€归崑鍕煕濞戞﹫宸ユい顐節濮婃椽宕崟鍨﹂梺璇茬箲缁诲牓骞冨Ο缁樺缂侇垱娲橀弬鈧梻浣虹帛閿氶柣蹇斿哺瀵娊鍩￠崨顔惧幈闁诲函缍嗛崜娆愮鏉堫煈娈介柣鎰綑婵秵顨ラ悙瀵稿闁瑰嘲鎳庨湁閻庯綆浜欐竟鏇㈡⒑閹稿孩绀€闁稿﹤缍婂畷鎰節濮橆厾鍙冨┑鈽嗗灟鐠€锕€危婵傚憡鐓欓柤鎭掑劜缁€瀣叏婵犲懏顏犵紒杈ㄥ笒铻ｉ柤濮愬€曞鎶芥⒒娴ｅ憡鍟為柤褰掔畺椤㈡牗寰勬繝鍕闂佸綊鍋婇崰姘卞閸忛棿绻嗘い鏍ㄧ鐠愶紕绱掗悩瀹犲妞ゎ亜鍟存俊鍫曞幢濞嗗浚娼风紓鍌欐祰椤曆囧磹閸喚鏆﹂柕澶堝妸娴滃綊鏌熼悜妯诲暗闁告ê宕埞鎴︽倷閸欏妫炵紓浣虹帛閸ㄨ儻妫㈤梺闈涚箚閸撴繈宕ｈ箛鎾斀闁绘ɑ褰冮弳鐐烘煏閸ャ劎绠栨い銊ｅ劦閹瑧鈧數顭堥埛宀勬⒑鐠団€虫灍闁荤啿鏅犻悰顕€骞樼拠鑼唺濠电娀娼ч悧鍐磻閹惧灈鍋撻棃娑欐喐缁惧彞绮欓弻娑氫沪閸撗勫櫗缂備椒鑳舵晶妤呭Φ閸曨垰鍗抽柛鈩冾殕婢跺嫰鏌涚€ｎ亶鍎旈柡宀€鍠栭幃娆擃敆閳ь剚鏅堕鐐寸厱闁靛牆妫欑粈鈧梺瀹狀潐閸ㄥ潡骞冮埡鍐ｅ亾閸︻厼孝妞ゃ儲绻堝娲川婵犲孩鐣锋繝鐢靛仜閿曨亜顕ｆ繝姘櫜濠㈣泛锕﹂惈鍕⒑閹肩偛鍔撮柣鎾崇墛缁傛帡鍩℃笟鍥ㄥ瘜闂侀潧鐗嗗Λ娆撳煕閹邦厾绠鹃柤纰卞墮閺嬪孩銇勯銏㈢闁圭厧缍婂畷鐑筋敇閻欏懐搴婂┑鐘殿暯濡插懘宕规导鏉戠妞ゆ劑鍎洪弶娲⒒閸屾艾鈧绮堟笟鈧獮澶愭晸閻樿尙顔囬柣鐘叉穿鐏忔瑩寮抽崱娑欑厵闂傚倸顕ˇ锔剧磼閻樺磭澧甸柡宀嬬秮婵偓闁靛繆鏅濋崝绋库攽閳藉棗浜濋柨鏇樺灲閻涱噣寮介鐔蜂壕婵炴垶顏伴幋鐘辩剨濞寸厧鐡ㄩ悡娑㈡煕鐏炵虎娈斿ù婊堢畺濮婂宕掑顑藉亾閻戣姤鍊块柨鏃堟暜閸嬫挾绮☉妯诲櫧闁活厽鐟╅弻鐔衡偓鐢殿焾琚ラ梺绋款儐閹告悂锝炲┑瀣亗閹肩补妲呭鐣岀磽閸屾瑧顦﹂柛濠傛贡閺侇噣鎮㈡俊鎾虫穿閵囨劙骞掗幘璺哄箰闁诲骸鍘滈崑鎾绘倵閿濆骸澧伴柣锕€鐗撻幃妤冩喆閸曨剙顦╂繛瀛樼矋缁诲牓鐛箛娑樺窛閻庢稒菤閹峰綊姊鸿ぐ鎺戜喊闁告挻宀搁幃妤呭箮閼恒儮鎷婚梺绋挎湰閻熴劑宕楀畝鈧惀顏堫敇閻愰潧鐓熼悗娈垮櫘閸撴氨绮悢鐓庣劦妞ゆ巻鍋撻柣锝囧厴楠炲鏁冮埀顒傜不婵犳碍鐓欏Λ棰佽兌閸斿秹鎮楅棃娑氱劯婵﹥妞藉Λ鍐ㄢ槈濮橆剦鏆繝纰樻閸嬪懘銆冩繝鍌滄殾闁告稑锕︾弧鈧梺鎼炲劘閸斿酣宕㈤柆宥嗙厽閹兼惌鍨崇粔鐢告煕鐎ｎ亝鍣归柣锝呭槻閻ｆ繈宕熼鍌氬箰闂佽绻掗崑娑欐櫠娴犲鐓″鑸靛姈閹虫岸鏌ㄥ┑鍡╂Ч闁绘挾濮撮埞鎴︻敊閽樺顫岄梺閫炲苯澧い銊ワ躬楠炲啴鍩勯崘銊х獮闂佸綊鍋婇崢濂稿焵椤掑倹鏆柟顔煎槻閳诲氦绠涢幙鍐х棯缂傚倷璁查崑鎾绘煕閹伴潧鏋熼柣鎾存礋閺屾洘绻涢崹顔煎闂佺顑冮崕鐢稿蓟閿濆應妲堥柛妤冨仦閻忓牓鏌ф导娆戠М闁哄被鍊曢湁閻庯綆鍋呴悵鏍⒑閹稿海鈯曠紒顔肩焸閸╃偤骞嬮敃鈧悡锟犳煕閳╁喚娈樺ù鐙€鍨跺娲川婵犲海鍔堕梺鍛婁緱閸犳鈻撻弶搴撴斀閹烘娊宕愰弴銏犵疇闊洦绋掗崑鍌炴煟閺傚灝鎮戦柣鎾跺枛閺岋繝宕掑☉姗嗗殝闂佺懓寮堕幃鍌炲蓟閿濆鏁囬柣鎴濇閸撳磭绱撴担浠嬪摵閻㈩垽绻濋妴浣糕枎閹炬潙娈熼梺闈涱槶閸庢娊銆傞弻銉︹拻濞达綁顥撴稉鑼磼闊厾鐭欑€规洘鐓″濠氬Ψ閳垛晛浜鹃柨鏇炲€告儫闂佸疇妗ㄧ欢姘跺船閸洘鈷戠紓浣股戦ˉ鍡樼箾閹捐櫕璐￠柤楦块哺缁绘繂顫濋娑欏濠电偠鎻紞鈧繛鍜冪悼閺侇喖鈽夐姀锛勫幍闂佸憡绺块崕娲倿妤ｅ啯顥嗗鑸靛姈閻撶喖鏌熸潏鍓у埌鐞氭岸姊虹粙娆惧剬闁哄懐濞€瀵鈽夊Ο閿嬫杸闂佺硶鍓濋〃蹇旂闁秵鈷戦悶娑掆偓鍏呭濠电偛顕慨鎾敄閸℃稒鍋傞柣鏂垮悑閻撴瑩姊洪銊х暠濠⒀屽枤缁辨帡鎮▎蹇斿闁绘挸鍟扮槐鎾存媴閼测剝鍨圭划鍫ュ焵椤掑嫭鈷掗柛銉戝本效缂備胶绮换鍐崲濠靛纾兼慨姗€妫跨槐鎴︽⒒娴ｈ銇熼柛妯恒偢閺佸啴顢旈崼婵婃憰闂佹枼鏅涢崯浼存偡瑜版帗鐓曢柕澶嬪灥鐎氼喛銇愭惔銏㈢瘈婵炲牆鐏濋弸娑㈡煥閺囶亜顩柛鎺撳浮椤㈡盯鎮欓懠顒夊數闂備礁鎲℃笟妤呭垂閹惰姤鍎楁繛鍡樻尰閻撴瑧绱掔€ｎ亞浠㈡い鎺嬪灲閺岋綁鏁傞懖鈺冃滃┑顔硷功缁垶骞忛崨鏉戝窛濠电姴鎳愰、鍛存⒒娴ｇ鏆遍柛銏＄叀閹囧箻瀹曞洦娈鹃梺鐟邦嚟閸嬬喓寮ч埀顒勬⒑缁嬫寧婀扮紒顔肩Т閳绘挻銈ｉ崘鈹炬嫼闂侀潻瀵岄崢濂搞€傞崗鑲╃瘈闁靛繆鍩楅鍫晩闊洦鎸撮弨浠嬫煕閳ュ磭绠查柡鍌楀亾闂傚倷鑳剁划顖濇懌閻熸粍婢橀崯鎾€侀弮鍌楀亾濞戞瑯鐒界紒鐘荤畺閺屸剝寰勭€ｎ亞浠村銈呮禋娴滅偟妲愰幒鏃傜＜婵☆垵鍋愰悿鍕攽閳藉棗浜滈柛鐕佸亰閸┿儲寰勬繛銏㈠枎閻ｂ剝锛愭担鍓叉闂傚倷绀佺紞濠偽涚捄銊х焼濞达綀娅ｆ稉宥夋煟閺傚灝鎮戦柣鎾卞劦閺岋綁寮幐搴㈠創閻庢稒绻堝铏圭磼濡闉嶅┑鐐跺皺閸犳牕顕ｆ繝姘亜闁告稑锕︾粔鑸典繆閵堝繒鍒伴柛鐕佸灦椤㈡挸顓兼径瀣ф嫼闂佽鍎兼慨銈夊极闁秵鍋ㄦい鏍ュ€楃弧鈧悗娈垮枦椤曆囧煡婢舵劕顫呴柍鍝勫€瑰▍鍥⒒娓氣偓濞佳囨偋閸℃稑绠犻煫鍥ㄧ⊕閸婄敻鏌ц箛鎾磋础缁炬崘妫勯湁闁挎繂娲﹂崵鈧梺鍛婃濞夋盯鈥旈崘顔肩骇闁瑰鍋涢弳鍫㈢磽娴ｅ摜鐒峰鏉戞憸閹广垹鈹戠€ｎ亞鍊為悷婊冪箻椤㈡瑥鐣濋崟顑芥嫼闂侀潻瀵岄崢濂稿礉鐎ｎ喗鐓涢悘鐐额嚙婵倿鏌熼鍡欑瘈闁诡喓鍨藉畷妤冧焊閺嶃劌顏归梻鍌欑閹诧紕鎹㈤崒婧惧亾濮樼厧骞栭柨鏇樺灲楠炲秹顢欓崜褝绱查梺璇插嚱缂嶅棝宕戦崟顐€褰掝敊缁涘顔旈梺缁樺姇瀵爼藟閵忊懇鍋撳▓鍨珮闁革綇绲介悾鐑芥偂鎼搭喚鍞靛銈呯箣閻掞箑鈻嶆繝鍥ㄧ厸閻忕偟鍋撶粈瀣偓瑙勬礈閸樠囧煘閹达箑鐐婇柤鍛婎問濡囨⒒閸屾瑧顦﹂柟纰卞亰椤㈡牠宕橀鑲╋紮闂佸搫绋侀崑鍛暦閸欏绡€闂傚牊绋掗ˉ鎴︽煛鐎ｎ偅顥堥柡灞剧洴閳ワ箓骞嬪┑鍥╀憾闂備浇顕х换鎰版偋閹炬剚娼栨繛宸簻缁犱即骞栧ǎ顒€鐏柨娑欙耿濮婅櫣绮欓崸妤€寮板┑鐐板尃閸ャ劌浠奸梺缁樺灱濡嫰宕￠幎鑺ョ厪闊洦娲栧暩濡炪倖鏌ㄩ幊妯侯潖濞差亜浼犻柛鏇ㄥ亐閸嬫捇宕奸弴鐐碉紮濠德板€曢崯浼存儗濞嗘挾鍙撻柛銉ｅ妽鐏忋劑鏌￠埀顒佺鐎ｎ偆鍘藉┑鈽嗗灡椤戞瑩宕电€ｎ兘鍋撶憴鍕仩闁稿海鏁诲濠氭晲閸涘倹妫冮崺鈧い鎺嗗亾閾荤偞绻濋棃娑卞剰缂佺姵鐗犻弻锝夊箛椤掍焦鍎撻梺鎼炲妼閸婂綊濡甸崟顖氱疀妞ゆ柨銇欓敍鍕＜闁靛鍎洪悡鍏兼叏婵犲啯銇濈€规洜鍏橀、姗€鎮欓幓鎺濈€遍梻鍌欑劍閹爼宕濇惔銊ユ瀬濠电姵鍝庨埀顑跨窔瀵噣宕煎┑鍫濆箰闂備礁鎲℃笟妤呭窗濮樿泛鐭?
                        Item {
                            Layout.fillWidth: true
                            height: mainContent.isPerformanceAnalysisView ? 580 : 500
                            
                            RowLayout {
                                anchors.fill: parent
                                spacing: 20
                                
                                // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤濠€閬嶅焵椤掑倹鍤€閻庢凹鍙冨畷宕囧鐎ｃ劋姹楅梺鍦劋閸ㄥ綊宕愰悙宸富闁靛牆妫楃粭鎺撱亜閿斿灝宓嗙€殿喗鐓￠、鏃堝醇閻旇渹鐢绘繝鐢靛Т閿曘倝宕幘顔肩煑闁告洦鍨遍悡蹇涙煕閳╁喚娈旈柡鍡到閳规垿鏁嶉崟顐㈠箣闂佺硶鏂侀崜婵嬪箯閸涱噮妲归幖杈剧到閳ь剛鍋ゅ濠氬磼濞嗘垹鐛㈠┑鐐板尃閸ャ劍娅栭棅顐㈡处缁嬫垵顪冮挊澹濆綊鏁愰崨顓涘彚闂佹寧姊婚弲顐ゅ姬閳ь剟姊哄Ч鍥х伈婵炰匠鍐╂瘎闂傚倷娴囧銊х矆娓氣偓楠炲鏁撻悩鑼杽闂侀潧艌閺呮稓绮荤紒妯镐簻闁哄啫娲ゆ禍褰掓煥濞戞瑧娲存慨濠呮閸栨牠寮撮悙娴嬫嫟缂傚倷绀侀鍡欐暜閳ュ磭鏆﹂柟鐗堟緲閸ㄥ倹銇勯弮鈧懝鍓х礊婵犲洤绠栭柍杞扮贰閸熷懏銇勯弮鍌氬付濠㈢懓顦靛缁樻媴閾忕懓绗￠梺鐟版憸閸嬫捇骞堥妸鈺佺妞ゆ棁鍋愰崢鎾⒑绾懏褰ч悗闈涚焸瀵偅绻濋崶銊у弮濠碘槅鍨拃锕€危閸涘浜滄い鎰╁灮閻掑憡鎱ㄦ繝鍐┿仢闁哄苯鎳橀幃娆撴嚑鐠轰警浼冨┑鐘媰鐏炵晫浠梺闈涙搐鐎氱増淇婇幖浣肝ㄧ憸宥壜烽埀顒傜磽閸屾瑧鍔嶆慨濠傤煼瀹曚即寮借閸ゆ洟鏌熺紒銏犳灈妞ゎ偄鎳橀弻宥夊Ψ閵婏妇褰ч梺姹囧€曢幊妯侯潖缂佹鐟归柍褜鍓欓…鍥樄缁℃捇鏌嶈閸撴稓妲愰幒妤佸亹闁告劘灏欐禒鏉戭渻閵堝棙绌跨紓宥勭閻ｇ柉銇愰幒婵囨櫓闁荤喐鐟ョ€氼參寮堕幖浣光拻濞达絽鎽滈弸鍐╀繆椤愩垹顏╅摶鐐翠繆閵堝懏鍣洪柛濠傜埣閺屻劑鎮㈤崫鍕戯綁鏌涙繝鍕毄缂佽鲸甯掕灃濞达綀銆€濡插牓姊洪悷鏉挎毐缂佺粯锚閻ｅ嘲螖閳ь剟鈥﹂妸鈺佺闁绘劦鍓氶ˉ鍫ユ煛娴ｇ懓濮嶇€规洏鍔戦、姗€鎮㈤崜鎻掓暭闂傚倸鍊烽懗鍓佸垝椤栫偛绠板Δ锝呭暙缁愭鏌″搴″箹闁藉啰鍠栭弻锟犲炊閵夈儳浠奸梻浣稿船濞诧妇鎹㈠┑瀣瀭妞ゆ劑鍨归～宥嗕繆閵堝棙顥堟慨濠勭帛閹峰懏顦版惔婵婎洬缂傚倷绀佸鍫曞磿閹惰В鈧棃宕橀埡鍐炬祫闁诲函缍嗘禍锝夊箺閺囥垺鈷戦柟绋挎捣閳藉鎮楀闂寸盎闁宠绮欓、鏃堝醇閻斿搫骞堥梻浣告贡閸嬫捇銆冮崨顖楀亾濮樼厧澧寸€规洝顫夊蹇涘Ω閵堝洨鐣鹃梻浣虹帛閸旓附绂嶅鍫濈劦妞ゆ帊绀侀悘瀵糕偓瑙勬礀缂嶅﹤鐣烽锕€绀夐柤鎭掑劜閵囨繈鏌熼鍝勭伈闁诡喒鍓濋幆鏃堝閳垛晛浜炬い鎺戝閳锋垿鏌涢敂璇插箹妞わ絽鍚嬬换婵嬪閳藉懓鈧潡鏌ｅ☉鍗炴珝濠殿喒鍋撻梺鎸庣☉鐎氼噣顢欓弴銏♀拺缂侇垱娲栨晶鏌ユ煏閸℃瑥浠辨鐐差儔閺佹劙宕掑顒傛▕闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁惧墽鎳撻—鍐偓锝庝簼閹癸綁鏌ｉ鐐搭棞闁靛棙甯掗～婵嬫晲閸涱剙顥氬┑掳鍊楁慨鐑藉磻閻愮儤鍋嬮柣妯荤湽閳ь兛绶氬鎾閳╁啯鐝栭梻渚€鈧偛鑻晶鎵磼椤旂⒈鐓兼い銏＄洴閹瑩寮堕幋鏂夸壕闁汇垹鎲￠悡銉︾節闂堟稒顥㈡い搴㈩殜閺岋紕鈧綆鍋嗛妴鎺旂磼鏉堛劌娴柛鈹惧亾濡炪倖甯掔€氼剟鏌嬮崶顒佺厽闁哄啫鍋嗛悞鍓р偓娈垮枛閻忔繈鍩為幋锕€鐓￠柛鈩冾殘娴狀參鏌ｆ惔锝囨嚄闁告侗鍠栭崢褰掓⒑閸涘﹥瀵欓柛娑卞灲缁辨煡姊绘担铏瑰笡闁挎洏鍨归…鍥槼缂佸倹甯￠弻鍡楊吋閸℃瑥骞愰梺璇茬箳閸嬬喖宕戦幘鍓佺焼闁告劦浜炵壕鑲╃磽娴ｈ鐒芥繛鎻掝嚟閳ь剝顫夊ú鏍礊婵犲洢鈧礁鈻庨幘鏉戜簵濡ょ姷鍎愰崰鎾诲磹閺嶎偅宕叉繝闈涱儐閸嬨劑姊婚崼鐔衡棩闁规潙鍢查—鍐Χ閸℃浠稿┑鐐跺皺閸犲酣鎮惧畡鎵虫斀閻庯綆浜為娲⒑閹稿孩纾甸柛瀣崌閺岋箓宕橀鍕€剧紓浣虹帛閻╊垶鐛€ｎ亖鏋庨煫鍥ㄦ磻閹綁姊绘担铏瑰笡閻㈩垱顨呴—鍐╃鐎ｎ亣鎽曞┑鐐村灟閸ㄥ湱绮诲☉娆嶄簻闁哄啫鐗婇敍鐔奉熆鐟欏嫭绀嬫慨濠勫劋鐎电厧鈻庨幋婵嗙厒濠电姭鎷冮崘鈺傚闯濠碘€冲级閸旀瑩鐛Ο灏栧亾濞戞顏堫敁閹剧粯鈷戦柛娑橈功缁犳捇鎮楀顒佸殗濠殿喒鍋撻梺闈涚箞閸ㄥ鏁嶅┑瀣€垫鐐茬仢閸旀岸鏌熼柨瀣仴妞ゆ柨绻樻俊鐑芥晜鏉炴壆鐩庢俊鐐€栭崝鎴﹀垂閼姐倗涓嶅┑鐘崇閻撴瑦銇勯弮鍌涙珪闁活厼锕幗鍫曟晲閸涱偀鍋撻幒鎴僵闁绘挸娴锋禒鈺佲攽閻愯尙澧曢柛姘儑閹广垹鈹戠€ｎ偄浠洪梻鍌氱墛缁嬫劗鍒掔捄琛℃斀闁宠棄妫楁禍婵囥亜閵娿儳澧︽鐐村灴婵偓闁靛牆鎳愰濠囨⒑閻熸壆锛嶆い鎺曞皺缁辩偞绗熼埀顒€顕ｉ锕€绠涙い鎾跺仧缁愮偤鏌ｆ惔顖涒偓銉╁礋椤愵偂绱梻鍌氬€搁崐椋庣矆娓氣偓楠炲鏁嶉崟顓犲闂佺鍕垫當缁炬儳娼″鍫曞醇濞戞ê顬嬫繝鈷€宥囩瘈婵﹤顭峰畷鎺戔枎閹烘垵甯紓鍌欑贰閸ｎ噣宕归崼鏇炍ラ柛鎰靛枛闁卞洭鏌ｉ弮鍌ょ劸鐎殿喖娼″娲捶椤撯剝顎楅梺鍝ュУ椤ㄥ﹤顕ｉ幓鎺嗘斀閻庯綆鍋嗛崢顏堟椤愩垺鍌ㄩ柛搴＄－婢规洟宕稿Δ浣哄幗闂佺粯妫冮ˉ鎾剁矓濞差亝鐓涢悘鐐垫櫕鍟稿銇卞倻绐旈柡灞剧洴楠炴鈧潧鎲￠崳浼存⒑閹稿海绠橀柛瀣Х缁鈽夊Ο閿嬵潔濠电偛妫欓崝鏇㈠礉閹烘鈷掑〒姘ｅ亾婵炶壈宕甸埀顒勬涧閻倸鐣烽姀掳鍋呴柛鎰ㄦ櫆濞呮牕鈹戦悩璇у伐闁哥喓濞€瀵劑鎳為妷锝勭盎闂佸搫鍟崐濠氬箺閸屾稓绠鹃柛顐ゅ枑閳锋劕菐閸パ嶈含妞ゃ垺娲熼弫鎰板炊閵娿儱鐏￠梺璇叉唉椤煤濡吋宕查柛鏇ㄥ幖閸ㄦ繃绻涢崱妯哄闁稿海鍠栭弻鐔煎箚瑜忛幗鐘绘煛婢跺鍊愭慨濠勭帛閹峰懘鎼归悷鎵偧闂備礁鎲″鐟懊洪弽顓ф晪闁挎繂顦柋鍥煛閸モ晛浠辨俊顐ゅ厴濮婅櫣绱掑Ο铏逛桓濡炪値鍘煎ú銊у垝缂佹ǜ鍋呴柛鎰ㄦ櫇閸樺崬鈹戦悙鍙夘棞婵炲瓨鑹惧嵄婵炲樊浜濋悡鐔搞亜閹捐泛鍓遍柛搴㈠灩缁辨帗娼忛妸銉﹁癁閻庤娲樼敮鎺楋綖濠靛柊鎺戔枍鐠囨彃顏存繛鍫滅矙閺岋綁骞囬澶婃婵犫拃灞藉缂佽鲸甯掕灃闁告劑鍔嶉悘鍫ユ倵鐟欏嫭绀€闁绘牕鍚嬫穱濠囧箹娴ｈ娅嗛梺浼欑到閼活垳绱為崱娑欌拻闁稿本鑹鹃埀顒佹倐瀹曟劖顦版惔銏╁仺濠殿喗锕╅崜锕傛倿閻ｅ本鍠愰幖娣妸閳ь剙鍟存俊鐑藉Ω瑜忛弶鎼佹⒑閸濆嫭宸濋柛锝冨劚椤洭鎮㈤崗灏栨嫼闂佸憡绻傜€氼參宕掗妸鈺傜厱闁靛鍎崇粔铏光偓瑙勬礃鐢剝淇婇幖浣哥厸闁稿本绮岄獮妤呮⒒娓氣偓濞佳呮崲閹烘挻鍙忛柣銏㈩焾閻ゎ噣鏌ｉ幇顔煎妺闁绘挸鍟村娲垂椤曞懎鍓扮紓浣诡殕鐢€澄涙担鐟扮窞闁归偊鍘鹃崣鍡椻攽閻樼粯娑ф俊顐ｇ懇瀹曞啿煤椤忓懐鍘甸梺鑲┾拡閸擄箓鎮炴ィ鍐╊梿濠㈣埖鍔栭悡鐔告叏濡も偓濡寮稿☉銏＄厽闊浄绲奸柇顖炴煛鐏炵偓绀嬬€规洘鍎奸ˇ鍙夈亜韫囷絽骞楁い銊ｅ劦閹瑥顔忛鐓庡闂備礁鐤囬～澶愬垂閸喚鏆﹂柟顖炲亰濡插ジ姊烘导娆戝埌妞ゎ厼鍢查～蹇撁洪鍛疅闂侀潧顦介崰姘跺礉閻戣姤鈷戦柛婵勫劚鏍￠梺缁橆殘婵炩偓闁绘侗鍣ｅ畷姗€濡告惔銏☆棃鐎规洏鍔戦、娆撴嚍閵壯冪闂傚倷鑳堕、濠囧磻閹邦喗鍋橀柕澶嗘櫅閻掑灚銇勯幒宥囶槮濠⒀屽灡缁绘盯鎮℃惔鈾€鎸冪紓浣介哺閹稿骞忛崨鏉戜紶闁告洦浜滈ˉ姘舵⒒娴ｄ警鐒鹃悗娑掓櫆缁绘稒绻濋崒婊勬闂侀潧锛忛埀顒勫磻閹剧粯鏅查幖绮瑰墲閻忓牊淇婂Δ鈧幊妯侯潖濞差亜宸濆┑鐘插暙椤︹晠姊洪幖鐐插濠㈢懓妫濋幃顕€骞嗚閸氬顭跨捄渚剳闁告﹢浜堕弻锝堢疀閺囩偘绮舵繝鈷€鍌滅煓闁诡噯绻濋、娑橆潩閼测晛鏁搁梺鑽ゅ枑閻熴儳鈧凹鍘剧划鍫⑩偓锝庡亝閸欏繐鈹戦悩鎻掓殲闁靛洦绻勯埀顒冾潐濞诧箓宕戞繝鍌滄殾闁绘梻鈷堥弫鍡椻攽閻樻彃顏い锔诲灦濮婂宕掑▎鎴М闂佺顕滅换婵嬬嵁閹邦厾绡€婵﹩鍘奸崜顔碱渻閵堝棛澧遍柛瀣洴瀵即濡烽埡鍌滃帗閻熸粍绮撳畷婊冾潩鐠轰綍锕傛煕閺囥劌鐏犵紒鐘茬－閳ь剙绠嶉崕鍗灻洪埡鍐幓婵°倕鎳忛埛鎴︽煕濠靛嫬鍔氶柡瀣捣缁辨帞绱掑鍫ｂ偓璺ㄢ偓娈垮枦椤曆囧煡婢舵劕顫呴柍銉ュ帠濮规姊绘担鐟板姢婵炲瓨宀稿畷鎴﹀幢濞存澘娲、姗€濮€閳锯偓閹锋椽姊洪崨濠勨槈闁挎洏鍊楃划顓烆潩閼搁潧浠哄銈嗙墬缁嬫垵霉椤曗偓閺屾盯鍩為崹顔句痪闂佽鍨卞Λ鍐╀繆閹间礁唯鐟滄粓宕€ｎ喗鈷掑ù锝囧劋閸も偓闂佸憡鍩冮崑鎾剁磽娴ｆ彃浜炬繛杈剧到濠€閬嶃€呴弻銉︾厵妞ゆ牕妫旂拋鏌ュ磻閹捐绠抽柟鎼幗閸嶇敻姊洪幐搴ｇ畵闁瑰啿绉电粩鐔煎即閵忥紕鍘介柟鍏肩暘閸╁嫰宕箛娑欑厱闁绘ɑ鍓氬▓婊勵殽閻愭潙濮嶉柟鐓庣秺椤㈡洟鏁愰崨顓濈礋闂傚倷绶氶埀顒傚仜閼活垱鏅堕幘顔界厸閻忕偟鏅晥闂佺硶鏂侀崜婵嬪箯閸涙潙宸濆┑鐘插枤閸炲綊姊婚崒娆掑厡缂侇噮鍨抽幑銏ゅ礃閳哄啰顔曟繝鐢靛Т閸嬪棗顭囬弽顐ょ＝濞达綀鍋傞幋鐐插灁闁圭虎鍠楅悡锝夌叓閸ャ劍灏甸柣锝庡弮閺屽秷顧侀柛鎾寸懇閺佸啴濡烽敂閿敵婵犵數濮村ú锕傚磻閹寸偟绠鹃柛鈩冾殘缁犳煡鏌熼崗鐓庡闁哄矉缍佹俊鍫曞炊瑜屾竟鏇犵磽娴ｈ櫣甯涢柣鈺婂灦閻涱喚鈧綆浜栭弸搴ㄧ叓閸ャ劍纾婚柟顕嗙悼缁?
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: 20
                                        
                                        // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁诡垎鍐ｆ寖闂佺娅曢幑鍥灳閺冨牆绀冩い蹇庣娴滈箖鏌ㄥ┑鍡欏嚬缂併劌銈搁弻鐔兼儌閸濄儳袦闂佸搫鐭夌紞渚€銆佸鈧幃娆撳箹椤撶噥妫ч梻鍌氬€稿ú銈壦囬悽绋胯摕闁靛鍎弨浠嬫煕閳╁啰鎳冩い銈傚亾濠碉紕鍋戦崐鏇犳崲閹烘挻鍙忓瀣椤洟鏌熼悜姗嗘當闁绘帒鐏氶妵鍕箳瀹ュ牆鍘＄紓浣叉閸嬫捇姊绘担渚劸闁哄牜鍓熼妴鍐幢濞戞ɑ顥濋梺閫炲苯澧存慨濠勭帛閹峰懘鎼归悷鎵偧闂備焦瀵у銊╁焵椤掍緡鍟忛柛鐘崇墵閹儲绺介崫銉ョウ闂佸搫绋侀崢鑲╃不濞戙垺鐓熸俊銈傚亾闁绘绻樺畷銏＄鐎ｎ偀鎷洪梻鍌氱墛缁嬫挻鏅堕弴鐔翠簻闁挎洖鍊烽幉楣冩煙椤旇棄鍔ら悡銈嗐亜韫囨挻鍣介柛妯绘倐閹宕楁径濠佸闂備線鈧偛鑻晶瀵糕偓瑙勬磻閸楁娊鐛鈧幊婊冣枔閹稿海绋愰梻鍌欑濠€閬嶅磿閵堝鍚归柨鏇炲€归崑鍕煕濞戞﹫宸ユい顐節濮婃椽宕崟鍨﹂梺璇茬箲缁诲牓骞冨Ο缁樺缂侇垱娲橀弬鈧梻浣虹帛閿氶柣蹇斿哺瀵娊鍩￠崨顔惧幈闁诲函缍嗛崜娆愮鏉堫煈娈介柣鎰綑婵秵顨ラ悙瀵稿闁瑰嘲鎳庨湁閻庯綆浜欐竟鏇㈡⒑閹稿孩绀€闁稿﹤缍婂畷鎰節濮橆厾鍙冨┑鈽嗗灟鐠€锕€危婵傚憡鐓欓柤鎭掑劜缁€瀣叏婵犲懏顏犵紒杈ㄥ笒铻ｉ柤濮愬€曞鎶芥⒒娴ｅ憡鍟為柤褰掔畺椤㈡牗寰勬繝鍕闂佸綊鍋婇崰姘卞閸忛棿绻嗘い鏍ㄧ鐠愶紕绱掗悩瀹犲妞ゎ亜鍟存俊鍫曞幢濞嗗浚娼风紓鍌欐祰椤曆囧磹閸喚鏆﹂柕澶堝妸娴滃綊鏌熼悜妯诲暗闁告ê宕埞鎴︽倷閸欏妫炵紓浣虹帛閸ㄨ儻妫㈤梺闈涚箚閸撴繈宕ｈ箛鎾斀闁绘ɑ褰冮弳鐐烘煏閸ャ劎绠栨い銊ｅ劦閹瑧鈧數顭堥埛宀勬⒑鐠団€虫灍闁荤啿鏅犻悰顕€骞樼拠鑼唺濠电娀娼ч悧鍐磻閹惧灈鍋撻棃娑欐喐缁惧彞绮欓弻娑氫沪閸撗勫櫗缂備椒鑳舵晶妤呭Φ閸曨垰鍗抽柛鈩冾殕婢跺嫰鏌涚€ｎ亶鍎旈柡宀€鍠栭幃娆擃敆閳ь剚鏅堕鐐寸厱闁靛牆妫欑粈鈧梺瀹狀潐閸ㄥ潡骞冮埡鍐ｅ亾閸︻厼孝妞ゃ儲绻堝娲川婵犲孩鐣锋繝鐢靛仜閿曨亜顕ｆ繝姘櫜濠㈣泛锕﹂惈鍕⒑閹肩偛鍔撮柣鎾崇墛缁傛帡鍩℃笟鍥ㄥ瘜闂侀潧鐗嗗Λ娆撳煕閹邦厾绠鹃柤纰卞墮閺嬪孩銇勯銏㈢闁圭厧缍婂畷鐑筋敇閻欏懐搴婂┑鐘殿暯濡插懘宕规导鏉戠妞ゆ劑鍎洪弶娲⒒閸屾艾鈧绮堟笟鈧獮澶愭晸閻樿尙顔囬柣鐘叉穿鐏忔瑩寮抽崱娑欑厵闂傚倸顕ˇ锔剧磼閻樺磭澧甸柡宀嬬秮婵偓闁靛繆鏅濋崝鎼佹⒑閸涘娈曞┑鐐诧躬瀵鏁愭径濠勵吅濠电娀娼уù鍌毼涢敓鐘崇厽閹兼番鍔嶅☉褔鏌ｉ鐐测偓鎼侊綖韫囨拋娲敂閸滀焦顥堟繝鐢靛仦閸ㄥ爼鎳濇ィ鍐︹偓鍌毭洪鍛嫼闂佸憡绺块崕鍗炩枍韫囨稒鐓曢悗锝庝悍瀹搞儵鏌ｉ敐鍛Щ闁宠鍨垮畷閬嶅煛閸屾艾鍘為梻鍌欒兌閸樠囧箺濠婂牆鏋侀柟闂寸閸屻劑鏌﹀Ο渚Т闁衡偓娴犲绠抽柟鎯版绾惧綊鏌熼崜褏甯涢柛濠傛健閺屻劑寮村Δ鈧禍楣冩⒑娴兼瑧鍒伴柛銏＄叀閹儳鈹戠€ｎ亞鍔﹀銈嗗笒鐎氼剟鎮為崹顐犱簻闁瑰搫绉剁拹浼存煕閻旈绠婚柡灞剧洴閹晛鐣烽崶褉鎷伴梻浣哄仺閸庤崵绮婚幋锔藉仼闁跨喓濮甸悞浠嬫煥閺囨浜惧┑鐐茬墑閸旀垵顫忓ú顏勬嵍妞ゆ挴鍓濋妤呮⒑閸濄儱校妞ゃ劌锕ら锝夊蓟閵夘喗鏅㈤梺鍛婃处閸撴盯宕㈤柆宥嗏拺闁告繂瀚崒銊╂煕閺傝法鐒搁柣娑卞枛铻栧ù锝堟閻﹀牓姊洪棃娑氱畾闁逞屽墮绾绢厽绂掗幘顔解拺閻庡湱濯鎰版煕閵娿儲鍋ユ鐐插暣閸╋繝宕ㄩ鐐碘偓顓烆渻閵堝棗濮х紒鏌ョ畺閹焦鎯旈埦鈧弨浠嬫煟濡澧柛鐔风箻閺屾盯鎮╅幇浣圭杹濡ょ姷鍋為崝娆忕暦濮椻偓閹崇娀顢楁担璇″晭闂傚倷绀佸﹢杈ㄦ櫠濡も偓椤灝螣閼测晙绗夐梺鑽ゅ枑閸ｇ銇愰幒鎾存珳闂佸憡渚楅崰妤呭窗閹扮増鈷戦柛娑橆煬閻掓儳顪冮弶鎴炴喐闁瑰箍鍨归埞鎴犫偓锝庝憾濞煎﹪姊洪幐搴ｇ畵婵☆偅鐩俊鎾箛閻楀牃鎷哄┑鐐跺蔼椤曆勬櫠閺屻儲鐓曢悗锝庡亜婵牏绱?
                                        Item {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 230
                                            Layout.minimumHeight: 210
                                            
                                            PositionsPanel {
                                                anchors.fill: parent
                                                positions: mainContent.effectivePositions
                                                totalMarketValue: Number(mainContent.liveAccountSnapshot.marketValue || 0)
                                                currencySymbol: mainContent.displayCurrencySymbol
                                                marketDataService: mainContent.marketDataService
                                            }
                                        }
                                        
                                        // 缂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁绘劦鍓欓崝銈囩磽瀹ュ拑韬€殿喖顭烽幃銏ゅ礂鐏忔牗瀚介梺璇查叄濞佳勭珶婵犲伣锝夘敊閸撗咃紲闂佺粯鍔﹂崜娆撳礉閵堝洨纾界€广儱鎷戦煬顒傗偓娈垮枛椤兘寮幇顓炵窞濠电姴瀚烽崥鍛存⒒娴ｇ懓顕滅紒璇插€块獮澶娾槈閵忕姷顔掔紓鍌欑劍椤洭宕㈡潏銊х瘈闁汇垽娼у瓭闂佺锕ょ紞濠傜暦閹达箑唯闁冲搫鍊婚崢鎼佹煟韫囨洖浠╂い鏇嗗嫭鍙忛柛灞惧閸嬫挸鈻撻崹顔界彯闂佸憡鎸鹃崰搴ㄦ偩閻ゎ垬浜归柟鐑樼箖閺呪晠鏌ｉ悢鍝ユ噧閻庢凹鍓熼幆渚€鏌嗗鍡忔嫽闂佺鏈悷褔宕濆鍡曠箚闁绘劕寮堕崑銉р偓瑙勬磸閸ㄨ櫣绮嬮幒鏂哄亾閿濆骸浜滃ù鐙€鍙冨娲礃閸欏鍎撻梺鐟板暱闁帮綁骞忛幋锔藉亜闁稿繗鍋愰崢浠嬫⒑閸濆嫬鈧悂鎮樺┑瀣畺闁硅揪闄勯悡鏇炩攽閻樻彃顏悽顖涚洴閺岀喐绗熼崹顔碱瀳闁剧粯鐗犻弻宥堫檨闁告挻鐟ㄩ悘瀣攽閻愬弶顥為柟绋款煼瀹曟垿鍩￠崨顔惧幗闂佺鎻徊鍊燁暱闂備焦濞婇弨閬嶅垂閸︻厽顫曢柟鐑樻煛閸嬫捇鏁愭惔鈥茶埅闂佺绨洪崕鐢稿蓟濞戞瑦鍎熼柨娑樺椤斿姊洪棃娑欐悙閻庢矮鍗抽悰顔锯偓锝庝簴閺€浠嬫煕閵夈劌鐓愰柨鐔村劦濮婄粯鎷呴悜妯烘畬缂備胶濮寸粔鐟扮暦绾懌浜归柟鐑樺灩閸樻挳姊虹涵鍛涧缂佺姵鍨块幃娆愮節閸ャ劎鍘撻梺鍛婄箓鐎氼剟寮抽悢闀愮箚闁圭粯甯楅崰妯绘叏婵犲嫮甯涢柟宄版噺缁楃喖顢涘鍐ㄐ梻鍌欑閹碱偊鎮у鍫濈婵炴垯鍨圭粈鍡涙煙閻戞ê鐏╅柡鍡楁閺屾盯寮村Δ浣规緬闂佺顑嗛幑鍥х暦閹烘鍊烽棅顐幘閻愬﹪姊绘担鍛婂暈婵炴彃绻樺畷婵嗩吋婢跺﹥顥濋梺閫炲苯澧ǎ鍥э躬閹瑩顢旈崟銊ヤ壕闁哄诞灞剧稁閻熸粎澧楃敮鈺呭极閸曨剛绠鹃柛鈩冾殕缁傚鏌ｉ幒宥囩煓闁哄瞼鍠栭獮宥夘敊绾拌鲸姣夐梻浣虹帛閹搁箖宕伴弽顓炶摕闁绘梻鍘ч崹鍌涖亜閺冨倵鎷″ù灏栧亾缂傚倸鍊风拋鎻掝瀶瑜斿畷鎴﹀箻缂佹鍘介柟鍏肩暘閸╁嫰宕箛娑欑厱闁绘ê纾晶鐢告煃閵夘垳鐣甸柟顔界矒閹稿﹥寰勫畝鈧弳顐︽⒒娓氣偓濞佳呮崲閸儱纾归柡宥庡幖绾惧鏌涘畝鈧崑娑氱不瑜版帒绾ч柛顐ｇ箓閳锋梻绱掓径灞炬毈闁哄本绋戦～婵嬵敆婢跺﹤澹夊┑鐑囩到濞层倝鏁冮鍫濈畺婵炲棙鎼╅弫鍌炴煕閺囨ê濡煎ù婊堢畺閺屸€崇暤椤斿吋婀扮紓宥呭缁绘繈鎮介棃娴讹綁鏌よぐ鎺旂暫闁诡噯绻濆鎾閿涘嫬骞堟俊鐐€栭崝妤佹叏閹绢喖绀夋慨姗嗗幗閸欏繘鏌ｉ悢鍛婄凡缂佺嫏鍥ㄧ厪闁搞儜鍐句純濡ょ姷鍋為敃銏犵暦閿熺姵鍊烽柛蹇撴憸濡垶姊婚崒娆戝妽濠电偛锕銊╂焼瀹ュ懎鐎梺闈╁瘜閸樹粙锝為弴銏＄厵闁绘垶蓱閻撴盯鏌涚€ｎ偅宕岄柡浣瑰姈閹柨鈹戦崼婵嗘瘓闂傚倷鑳堕…鍫ヮ敄閸愵喖纾块柡灞诲劚妗呴梺鍛婃处閸撴岸宕曢悢鍏肩厪闊洤锕ュ▍鍡涙煟閵堝嫮顦﹂柍瑙勫灴椤㈡瑧娑甸悜鐣屽弽婵犵數鍋涢幏鎴犵礊娴ｅ壊鍤曟い鎰跺瘜閺佸鏌嶈閸撶喖濡存担绯曟瀻闁规儳纾ˇ顓烆渻閵堝骸澧婚柛鐘愁殘閸掓帡骞樼拠鑼暫濠德板€愰崑鎾绘煃缂佹ɑ宕岀€规洖缍婇、娆撴偩鐏炲ジ鍋楁繝纰夌磿閸嬫垿宕愰妶澶婂偍濡わ絽鍟粈鍌涙叏濡炶浜鹃梺缁樹緱閸ｏ絽鐣峰鈧、娆戝枈鏉堛劎绉遍梻鍌欑窔濞佳呮崲閸℃稑鐒垫い鎺嗗亾闁告ɑ鐗滈崚鎺曨樄婵﹦绮幏鍛村传閸曨亞绱﹂梻浣侯焾闁帮絾绂嶇捄铏规殾闁靛繈鍊曢崘鈧梺闈浤涢崨顓㈢崕闂傚倷绀侀幖顐⒚洪姀銈呭瀭婵炲樊浜滈悡鏇㈡煙鐎电浠﹂柛娆忕箻閺岋綁鎮㈤崫鍕垫毉闂佺懓鍚嬮崝娆撳蓟閿濆牏鐤€闁挎繂瀚崙褰掓⒑闂堟稒鎼愰悗姘緲椤曪綁顢氶埀顒勫春閳ь剚銇勯幒鎴濐仾闁稿顑夐弻锝呂熷▎鎯ф缂備胶濮甸悧鐘诲蓟閿濆绠涙い鎺嶇劍閸庢挾绱撴担鍝勑ｇ痪鏉跨Ч婵＄敻宕熼锝嗘櫈闁荤喐鐟辩徊鑺ョ閸撗€鍋撶憴鍕婵炲眰鍔嶉〃娆撴⒒閸屾瑦绁版い鏇熺墵瀹曚即骞掑Δ鈧悿鐐箾閹存瑥鐏柛瀣ф櫊閺岋綁骞嬮敐鍡╂闂佺粯鍔曢敃顏堝蓟瀹ュ浼犻柛鏇ㄥ亝濞堫參鏌ｉ姀鈺佺仩闂佸府绲介～蹇旂節濮橆剛锛滃┑顔斤供閸忔﹢宕戦幘鍓佺煓婵☆偄鐏氬鑺ヤ繆閼哥數鐝堕柨鏇楀亾婵炲樊鍙冮獮鍐偩瀹€鈧惌娆撴煠閹颁礁鐏￠柟韫嵆濮婄粯鎷呴悷閭﹀殝缂備浇顕ч崐鍧楃嵁婢跺娼ㄩ柍褜鍓熼獮鍐閵堝懍绱堕梺鍛婃处閸撴盯鍩€椤掍礁鈻曟慨濠冩そ瀹曘劍绻濇惔銏㈡毉闂備胶顭堥鍡涙儎椤栫偞鏅查柣鎰▕濞尖晠鏌ら崫銉毌闁归绮换娑欐綇閸撗呅氬┑鈽嗗亜鐎氼厾绮嬪澶嬪仭闂侇叏濡囬崬鐢告偡濠婂啴鍙勯柕鍡楀暣瀹曞ジ濡烽妷褍濮︽俊鐐€栫敮鎺楀磹婵犳艾绠犳俊銈呮噺閻撴洟鏌曟径瀣仴闁硅櫕鍔欏鑸电鐎ｎ偀鎷绘繛鎾村焹閸嬫挻绻涙担鍐叉礌閳ь剨绠撻、姗€鎮㈤崜浣虹暰闂備焦鎮堕崕顕€寮插☉娆愬弿妞ゆ帒瀚悡鍐喐濠婂牆绀堥柣鏂款殠濞兼牠鏌ц箛鎾磋础闁活厽鐟╅弻銈夊箛娴ｅ摜浼囧┑鐐靛帶閻栫厧顫忛搹鍦煓闁告牑鍓濋弫鎯ь渻閵堝啫濡奸柨鏇樺€濋幃楣冩倻閽樺）銊ф喐婢舵劕纾婚柟鍓х帛閺呮煡骞栫划鐟板⒉闁诲繐绉瑰铏圭矙閸栤€冲闂佽桨绀侀…鐑界嵁閸愩劎鏆嬮柟浣冩珪閻庡妫呴銏″闁瑰皷鏅滅粋鎺楀礈瑜忕壕钘壝归敐鍡楃祷濞存粓绠栧娲礈閹绘帊绨撮梺绋垮閻擄繝骞冮敓鐘插嵆闁绘柨澧庣粻姘渻閵堝棛澧紒顔艰嫰閻☆厽绻濋悽闈涗粶闁活亙鍗冲畷鎰攽鐎ｎ亞鐣洪梺璺ㄥ枔婵挳鎮橀幎鑺ョ厵濡娴囬崗宀勬煕閻愬灚鏆柡宀嬬秮閹晠宕ｆ径瀣壍闂備線娼ч悧鍛垝濞嗘挸鏋侀柟鍓х帛閸嬫劙鏌涢幇顖氱处缂併劌顭峰缁樻媴娓氼垳鍔哥紓浣虹帛閸旀瑩骞嗛崘顔藉€婚柦妯侯槺椤斿棙绻濋悽闈浶ｇ痪鏉跨Ч閹繝鎮㈤悡搴ｎ啇濠电儑缍嗛崜娆愪繆閼恒儳绠鹃柟鍐插槻濞诧箓鎮″▎鎾村仯闁搞儱娲ら幊鎰版儊閸儲鈷戦弶鐐村椤︼妇绱掓径鎰垫缂侇喛顕ч埥澶愬閻樼數娼夐梻浣侯焾閺堫剛鍒掓惔鈭?
                                        Item {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            visible: !mainContent.isTradeRecordsView
                                            
                                            StrategiesPanel {
                                                anchors.fill: parent
                                                strategies: mainContent.effectiveStrategies
                                            }
                                        }
                                    }
                                }
                                // 闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻鐔兼⒒鐎靛壊妲紒鐐劤缂嶅﹪寮婚悢鍏尖拻閻庨潧澹婂Σ顔剧磼閻愵剙鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲酣娼ф竟濠偽ｉ鍓х＜闁诡垎鍐ｆ寖闂佺娅曢幑鍥灳閺冨牆绀冩い蹇庣娴滈箖鏌ㄥ┑鍡欏嚬缂併劌銈搁弻鐔兼儌閸濄儳袦闂佸搫鐭夌紞渚€銆佸鈧幃娆撳箹椤撶噥妫ч梻鍌欑窔濞佳兾涘▎鎴炴殰闁圭儤顨愮紞鏍ㄧ節闂堟侗鍎愰柡鍛叀閺屾稑鈽夐崡鐐差潻濡炪們鍎查懝楣冨煘閹寸偛绠犻梺绋匡攻椤ㄥ棝骞堥妸鈺傚€婚柦妯侯槺閿涙稑鈹戦悙鏉戠亶闁瑰磭鍋ゅ畷鍫曨敆娴ｉ晲缂撶紓鍌欑椤戝棛鈧瑳鍥ㄥ€垫い鎺戝閳锋垿鏌ｉ悢鍛婄凡闁抽攱姊荤槐鎺楊敋閸涱厾浠搁悗瑙勬礃閸ㄥ潡鐛崶顒佸亱闁割偁鍨归獮鍫ユ⒒娴ｅ摜绉洪柛瀣躬瀹曞綊骞嶉绛嬫綗闂佹寧娲栭崐褰掓偂閻斿吋鐓忛煫鍥ㄦ礀椤庡矂鏌ｉ幘鍐叉倯闁逛究鍔嶇换婵嬪礋椤撶偟顐肩紓鍌欑劍椤ㄥ牓宕伴弽顓炴槬闁逞屽墯閵囧嫰骞掗崱妞惧婵＄偑鍊ら崢鐓幟洪埡鍚藉洩銇愰幒鎾崇檮濠电娀娼уú銏＄濠婂牊鐓欓柡澶婄仢椤ｆ娊鏌ｉ敐澶夋喚闁哄矉缍佹俊鍫曞炊瑜屾竟鏇犵磽娴ｈ櫣甯涢柣鈺婂灦閻涱喚鈧綆浜栭弸搴ㄧ叓閸ャ劍纾婚柟顕嗙悼缁辨挻鎷呴崫鍕闂佺瀛╂繛濠冧繆閸洖绠瑰ù锝嗙摃閹芥洟姊洪崫鍕窛闁哥姵鎸剧划缁樸偅閸愨晝鍘介梺閫涘嵆濞佳勬櫠椤栫偞鐓熸繝闈涙处缁€瀣叏婵犲懏顏犵紒杈ㄥ笒铻ｉ悹鍥ㄧ叀閻庤櫣绱撻崒娆戭槮妞ゆ垵妫濋獮鎴﹀炊椤掆偓閺勩儵鏌嶈閸撴岸濡甸崟顖氱闁糕剝銇炴竟鏇犵磽閸屾瑨鍏屽┑顔碱嚟缁棃鎮烽幍顔芥闂佸搫娲ㄩ崰鎰礊閸ャ劊浜滄い鎾跺枎閻忥附銇勯弮鈧Λ鍐潖妤﹁￥浜归柟鐑樻惈缁辩敻姊洪悡搴ｆ瀮婵炲鐩幃楣冨垂椤愩倗鎳濋梺閫炲苯澧寸€殿喖顭烽崹楣冨箛娴ｅ憡鍊梺纭呭亹鐞涖儵鍩€椤掆偓绾绢參顢欓幋锔解拻濞达綀娅ｇ敮娑㈡煙閹间胶鐣虹€规洑鍗冲浠嬵敇閻愯埖鎲伴梻浣告惈濞层垽宕硅ぐ鎺撶厑闁搞儯鍔庣弧鈧梺鍓茬厛閸嬪嫭鎱ㄩ崼婢棃寮崼鐔叉嫽婵炶揪缍侀ˉ鎾寸▔閼碱剛纾奸柛娆忣槸閸斻倖銇勯鍕殲缂佸倹甯為埀顒婄秵娴滐綁骞楅弴銏♀拺闁圭娴烽埥澶愭倵濮橀棿绨婚柍璁崇矙椤㈡棃宕奸悢鍝勫箞闂備礁婀遍崑鎾汇€冮崨顖楀亾濮樼厧澧寸€规洝顫夊蹇涘Ω閵堝洨鐣鹃梻浣虹帛閸旓附绂嶅鍫濈劦妞ゆ帊绀侀悘瀵糕偓瑙勬礀缂嶅﹤鐣烽锕€绀夐柤鎭掑劜閵囨繈鏌熼鍝勭伈闁诡喒鍓濋幆鏃堝閳垛晛浜炬い鎺戝閳锋垿鏌涢敂璇插箹妞わ絽鍚嬬换婵嬪閳藉懓鈧潡鏌ｅ☉鍗炴珝濠殿喒鍋撻梺鎸庣☉鐎氼噣顢欓弴銏♀拺缂侇垱娲栨晶鏌ユ煏閸℃瑥浠辨鐐差儔閺佹劙宕掑顒傛▕闂傚倸鍊搁崐鎼佸磹閹间礁纾归柟闂寸绾惧綊鏌熼梻瀵割槮缁惧墽鎳撻—鍐偓锝庝簼閹癸綁鏌ｉ鐐搭棞闁靛棙甯掗～婵嬫晲閸涱剙顥氬┑掳鍊楁慨鐑藉磻閻愮儤鍋嬮柣妯荤湽閳ь兛绶氬鎾閳╁啯鐝栭梻渚€鈧偛鑻晶鎵磼椤旂⒈鐓兼い銏＄洴閹瑩寮堕幋鏂夸壕闁汇垹鎲￠悡銉︾節闂堟稒顥㈡い搴㈩殜閺岋紕鈧綆鍋嗛妴鎺旂磼鏉堛劌娴柛鈹惧亾濡炪倖甯掔€氼剟鏌嬮崶顒佺厽闁哄啫鍋嗛悞鍓р偓娈垮枛閻忔繈鍩為幋锕€鐓￠柛鈩冾殘娴狀參鏌ｆ惔锝囨嚄闁告侗鍠栭崢褰掓⒑閸涘﹥瀵欓柛娑卞灲缁辨煡姊绘担铏瑰笡闁挎洏鍨归…鍥槼缂佸倹甯￠弻鍡楊吋閸℃瑥骞愰梺璇茬箳閸嬬喖宕戦幘鍓佺焼闁告劦浜炵壕鑲╃磽娴ｈ鐒芥繛鎻掝嚟閳ь剝顫夊ú鏍礊婵犲洢鈧礁鈻庨幘鏉戜簵濡ょ姷鍎愰崰鎾诲磹閺嶎偅宕叉繝闈涱儐閸嬨劑姊婚崼鐔衡棩闁规潙鍢查—鍐Χ閸℃浠稿┑鐐跺皺閸犲酣鎮惧畡鎵虫斀閻庯綆浜為娲⒑閹稿孩纾甸柛瀣崌閺岋箓宕橀鍕€剧紓浣虹帛閻╊垶鐛€ｎ亖鏋庨煫鍥ㄦ磻閹綁姊绘担铏瑰笡閻㈩垱顨呴—鍐╃鐎ｎ亣鎽曞┑鐐村灟閸ㄥ湱绮诲☉銏＄厱闁规崘灏崗宀€绱掗幇顓ф當闁宠鍨块幃娆撴嚑椤掑倸濮煎┑鐐茬摠缁姵绂嶅鍫稏闊洦鎷嬪ú顏嶆晜闁告洦鍋嗛悰鈺佲攽閻樺灚鏆╁┑顔芥尦瀹曟劗绱掑Ο纰辨祫濡炪倖鐗楃划搴ｅ閽樺鈧帒顫濋浣规倷闂佸搫顑囬崰鏍蓟閿濆鍋勯柡澶嬪灥婵箓姊洪幐搴ｇ畼闁稿濮风划璇测槈閵忕姷鐫勯梺绋挎湰缁骸危閸ャ劎绡€婵炲牆鐏濋弸鐔兼煥閺囨娅婄€规洘绮岄埥澶愬閳╁啯鐝繝鐢靛Т閿曘倝宕悩璇茬哗闁兼亽鍎禍婊堟煛閸愶絽浜鹃梺鍝勵儑缁垳绮悢灏佹闁靛骏绱曢崢鍗炩攽閻愬弶顥滅紒缁橈耿椤㈡挸螖閸涱喗鍤夐梺缁樺姉閸庛倝鎮￠悢鍏肩厵闂侇叏绠戦弸鐔兼煛閸″繑娅囬柣銉邯瀹曪綁濡疯閻撴捇姊洪崫鍕拱缂佸鎸荤粋鎺楁晝閸屾稑鈧攱銇勯幒鎴濃偓褰掝敇婵犳碍鐓熼幖娣灮閳洜绱掔拠鑼妞ゎ偄绻愮叅妞ゅ繐瀚鍥煙閸忚偐鏆橀柛銊ョ秺钘濋柍鍝勫€荤粻楣冩倵濞戞瑯鐒介柣顓烆儑缁辨帡顢欓懞銉ョ３闂侀潧妫旂粈渚€锝炲┑瀣殝闁割煈鍋呴悵鎶芥⒒娴ｈ櫣銆婇柛鎾寸箞閹柉顦归柟顖欑窔瀹曠厧鈹戦崘鈺傛澑婵＄偑鍊栧褰掑几缂佹鐟规繛鎴欏灪閻撴洘淇婇娑橆嚋妞ゃ儱顦甸弻宥囨嫚閼碱儷銏°亜椤撴粌濮傜€规洜鍠栭、姗€鎮╅崹顐綋闂傚倸鍊风欢姘焽瑜庨〃銉ㄧ疀閺囩偟绛忔繛瀵稿Т椤戝棝宕戦崒鐐寸厵闁规鍠栭。濂告倵濮橆剚鍤囨慨濠冩そ椤㈡鍩€椤掑倻鐭撻柣銏犳啞閸嬪倹绻涢幋娆忕仾闁绘挻娲樼换娑㈠箣濠靛棜鍩為梺鍝勵儍閸婃繈寮婚敐澶樻晣闁绘棃顥撻悷鏌ユ⒑闁稓鈹掗柛鏂跨Ф閹广垹鈹戠€ｎ亜绐涘銈嗘礀閹冲秹宕Δ鍛拻濞达絽鎲￠崯鐐烘煟濡や緡娈滈柟顔ㄥ洦鍋愮€瑰壊鍠栧▓銊╂⒑閸︻叀妾搁柛鐘愁殜瀹曟劙鏌ㄧ€ｎ剛顔曢梺鍝勵槹閸╁牓宕曢幇鐗堝€垫慨妯哄船閺嬪酣鏌嶇憴鍕伌闁轰礁绉瑰畷鐔碱敃閳╁啯绶氶梻鍌欒兌椤牓鏁冮妷鈺佸瀭闁割煈鍠氶弳锔界節闂堟稓澧旀繛宀婁邯瀵爼鎮欓弶鎴殝缂備礁顑嗛崹鍧楀春閻愬搫绠ｉ柣姗嗗亜娴滈箖鏌ㄥ┑鍡欏嚬缂併劋绮欓弻锝夋晲閸涱喗鍎撻梺瀹狀潐閸ㄥ潡銆佸▎鎰弿闁归偊浜為幑鏇㈡⒒娴ｄ警鐒炬い鎴濇楠炴劖銈ｉ崘銊у姦濡炪倖甯掗崰姘焽閹邦厾绠鹃柛娆忣槺婢ь亪鏌ｉ敐鍥у幋妞ゃ垺顨婂畷姗€顢旈崘顓炵劵闂傚倸鍊搁崐椋庣矆娓氣偓瀹曘儳鈧綆鍓涢惌鍫ユ煙缂併垹鏋涢柣銈夌畺閺屽秹宕崟顒€娅ｉ梺姹囧€ら崳锝夌嵁閺嶎灔搴敆閳ь剚淇婃禒瀣厓闂佸灝顑呴悘鎾煙椤旇偐绉虹€规洘鍎奸ˇ鑼磼閻欐瑥娲﹂悡娆撴煟瑜嶉幗婊呯矓椤曗偓閺岋紕浠︾粙鍨拤闂佺懓鍢查幊鎰板箟閹绢喖绀嬫い鎰枎娴滈箖鏌熼幍顔碱暭闁绘挸鍟撮弻鏇熷緞濡櫣浠梺浼欑悼閺佹悂鍩€椤掑喚娼愭繛鍙夌矒瀹曘垼顦归柛鈺冨仱楠炲鏁冮埀顒勬煁閸ヮ剚鐓熼柡鍐ㄥ亞閻掔偓绻涢崨顔肩伌婵﹤顭峰畷鎺戭潩椤戣棄浜鹃柟闂寸绾惧綊鏌熼梻瀵割槮缁炬儳缍婇弻锝夊箣閿濆憛鎾绘煕閵堝懎顏柡灞剧洴楠炴﹢鎳犻澶嬓滈梻浣规偠閸斿秶鎹㈤崘顔嘉﹂柛鏇ㄥ灠閸愨偓濡炪倖鍔﹀鈧柡澶樺弮濮婃椽鏌呴悙鑼跺濠⒀屽櫍閺屾盯鎮㈤崨濠勭▏闂佷紮绲块崗妯讳繆閹间礁鐓涘┑鐘插暞濞呮牗绻濋悽闈涗沪闁搞劌鐖奸弫瀣磽娴ｉ潧濡搁柛搴ゅ皺閹广垹鈹戦崱蹇旂亖闂佸壊鐓堥崰妤呮倶瀹ュ鈷戦梻鍫熺⊕婢跺嫰鏌ｉ埡濠傜仩闁伙絿鍏橀獮瀣晝閳ь剛绮婚懡銈囩＝濞达綀顕栭悞浠嬫煕濡寧顥夐柍瑙勫灴閹瑩寮堕幋鐘辨樊闁诲氦顫夊ú锕傚礈閻斿鍤曢柟闂寸劍閺呮粓鏌涢幘妤€鎷戠槐鏌ユ⒒娴ｈ櫣甯涢柨姘辩棯缂併垹寮柛鈹垮劜瀵板嫰骞囬鐘插笚闂備浇濮ら敋妞わ箒妫勫嵄閻熸瑥瀚粻楣冩煟閹捐櫕鎹ｇ紒鐘侯嚙閳规垿鍩勯崘鈺佲偓鎰版煛娴ｇ鈧潡骞冮崜褌娌柣锝呮湰閸嬔囨⒒閸屾艾鈧悂宕愰幖浣哥９闁归棿绀佺壕褰掓煟閹达絽袚闁稿﹤娼￠弻銊╁籍閸喐娈伴梺绋款儐閹稿墽鍒掗鐐╂婵☆垰鎼粻濠氭⒑閸涘﹦绠撻悗姘卞厴瀹曘儳鈧綆浜堕悢鍡涙偣閾忕懓鐨戦柛鏃傚枛閺屻劌鈽夊▎鎴炲垱濠殿喖锕ㄥ▍锝夊箟閹绢喖绀嬫い鎰╁灩琚樻繝鐢靛Л閹峰啴宕橀埡鍌氶棷濠电姰鍨奸～澶娒哄Ο鑲╃处濞寸姴顑呭婵嗏攽閻樻彃顏╅悽顖涱殜閺岋綁鎮㈤崫銉х厑缂備緡鍠楅幐鎼佹偩瀹勯偊鐓ラ柛顐ゅ枎娴滄粍淇婇悙宸剰閻庢稈鏅涢埢鎾诲箚瑜夐弨鑺ャ亜閺傛娼熷ù鐘崇矒閺屾稓鈧綆鍋呭畷宀勬煛鐏炲墽娲撮柛鈺嬬節瀹曟﹢濡搁妶鍡楀闂佽楠搁悘姘熆濡皷鍋撳顓熺凡闁伙絿鍏橀、鏇㈡晝閳ь剙鏁柣鐔哥矊闁帮絽顕ｉ弻銉ノ╅柍鍝勫€甸幏濠氭⒑缁嬫寧婀伴柤褰掔畺閸┾偓妞ゆ帒瀚峰Λ鎴犵磼椤旇偐澧涚紒妤冨枛閸┾偓妞ゆ帒瀚畵渚€鏌″搴″季闁轰礁鍟撮弻銊╁即濡も偓娴滃墽绱掗悙顒€鍔ょ紓宥咃躬瀵鎮㈤崗灏栨嫽闁诲海鏁搁…鍫熶繆娴犲鈷戠紒瀣皡姒岸鏌涢埡鍌滃⒈闁瑰箍鍨归埞鎴犫偓锝庡亽濡啫鈹戦悙鏉戠仴鐎规洦鍓熷畷婊堝箥椤斿墽锛濇繛杈剧到閹碱偅鐗庨梻浣虹帛椤ㄥ牊绻涢埀顒傗偓娈垮枛椤兘骞冮姀銏″仒闁炽儱鍘栨竟鏇㈡⒑濮瑰洤鐏い鏃€鐗犻幃鐐哄箚椤紕鎳撻…銊╁川椤撶偘绮梻鍌氭搐椤︽壆鎹㈠┑鍥╃瘈闁稿本鍑规导鈧梻浣规た閸樼晫鏁敓鐘茶摕闁挎繂顦粻濂告煕閹扳晛濡洪柛鐘诧躬閹鎲撮崟顒傤槬闂佺粯鐗曢崥瀣┍婵犲洤绠瑰ù锝堫潐濞呭棛绱撻崒娆撴闁搞劑缂氶埅鏌ユ⒒閸屾艾鈧悂宕愭搴ｇ焼濞撴埃鍋撴鐐寸墵椤㈡洟鏁傜紒妯绘珗闂備胶纭堕崜婵嬫偡瑜旈幆灞解枎閹惧鍘甸梺缁樺灦閿曗晛鈻撻弮鍌滅＜?
                                Item {
                                    Layout.preferredWidth: mainContent.pageMode === "live-trading" ? 320 : 300
                                    Layout.minimumWidth: 280
                                    Layout.maximumWidth: 320
                                    Layout.fillHeight: true
                                    visible: mainContent.isPerformanceAnalysisView

                                    Rectangle {
                                        anchors.fill: parent
                                        visible: mainContent.isPerformanceAnalysisView
                                        radius: 16
                                        color: "#121828"
                                        border.color: "#2d3748"
                                        border.width: 1

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 24
                                            spacing: 16

                                            Text {
                                                text: mainContent.liveText("performanceBreakdown")
                                                color: "#f1f5f9"
                                                font.pixelSize: 15
                                                font.weight: Font.Medium
                                            }

                                            Repeater {
                                                model: [
                                                    { label: mainContent.liveText("accountAssets"), value: mainContent.currencyText(mainContent.liveAccountSnapshot.totalAsset || 0) },
                                                    { label: mainContent.liveText("availableCash"), value: mainContent.currencyText(mainContent.liveAccountSnapshot.availableCash || 0) },
                                                    { label: mainContent.liveText("marketValue"), value: mainContent.currencyText(mainContent.liveAccountSnapshot.marketValue || 0) },
                                                    { label: mainContent.liveText("turnover"), value: mainContent.currencyText(mainContent.liveTurnover || 0) }
                                                ]

                                                Rectangle {
                                                    id: breakdownCard
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 64
                                                    radius: 10
                                                    color: "#1a2235"

                                                    Column {
                                                        anchors.fill: parent
                                                        anchors.margins: 14
                                                        spacing: 6

                                                        Text {
                                                            text: breakdownCard.modelData.label || ""
                                                            color: "#94a3b8"
                                                            font.pixelSize: 12
                                                        }

                                                        Text {
                                                            text: breakdownCard.modelData.value || ""
                                                            color: "#f8fafc"
                                                            font.pixelSize: 17
                                                            font.weight: Font.Medium
                                                        }
                                                    }
                                                }
                                            }

                                            Item { Layout.fillHeight: true }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: mainContent.pageMode === "live-trading" ? 40 : 0
                visible: mainContent.pageMode === "live-trading"

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    anchors.verticalCenter: parent.verticalCenter
                    height: 34
                    radius: 10
                    color: "#0f172a"
                    border.color: mainContent.liveLatestAccountOrderMessage.length > 0 ? "#3b82f6" : "#334155"
                    border.width: 1
                    clip: true

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 10
                        spacing: 10

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: mainContent.liveAccountOrderCount > 0 ? "#22c55e" : "#64748b"
                        }

                        Rectangle {
                            radius: 8
                            color: mainContent.liveAccountOrderCount > 0 ? "#12263f" : "#1f2937"
                            border.color: mainContent.liveAccountOrderCount > 0 ? "#3b82f6" : "#475569"
                            border.width: 1
                            implicitWidth: marqueeTagText.implicitWidth + 12
                            implicitHeight: 22

                            Text {
                                id: marqueeTagText
                                anchors.centerIn: parent
                                text: mainContent.liveText("liveBroadcast")
                                color: "#93c5fd"
                                font.pixelSize: 11
                                font.weight: Font.Medium
                            }
                        }

                        Item {
                            id: liveMessageViewport
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            Text {
                                id: liveMessageTicker
                                y: (liveMessageViewport.height - height) / 2
                                x: implicitWidth > liveMessageViewport.width
                                    ? liveMessageViewport.width
                                    : 0
                                text: mainContent.liveMarqueeText
                                color: mainContent.liveAccountOrderCount > 0 || mainContent.livePositionCount > 0 ? "#e5e7eb" : "#94a3b8"
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                width: implicitWidth

                                NumberAnimation on x {
                                    running: mainContent.pageMode === "live-trading" && liveMessageTicker.implicitWidth > liveMessageViewport.width
                                    from: liveMessageViewport.width
                                    to: -liveMessageTicker.implicitWidth
                                    duration: Math.max(10000, liveMessageTicker.implicitWidth * 20)
                                    loops: Animation.Infinite
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 18
                                color: "#0f172a"
                            }

                            Rectangle {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 24
                                color: "#0f172a"
                            }
                        }
                    }
                }
            }
        }
    }
}



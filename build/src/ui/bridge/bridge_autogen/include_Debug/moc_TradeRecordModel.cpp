/****************************************************************************
** Meta object code from reading C++ file 'TradeRecordModel.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.3.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../../src/ui/bridge/include/TradeRecordModel.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TradeRecordModel.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.3.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_TradeRecordModel_t {
    uint offsetsAndSizes[30];
    char stringdata0[17];
    char stringdata1[13];
    char stringdata2[1];
    char stringdata3[9];
    char stringdata4[7];
    char stringdata5[6];
    char stringdata6[7];
    char stringdata7[5];
    char stringdata8[6];
    char stringdata9[10];
    char stringdata10[9];
    char stringdata11[11];
    char stringdata12[10];
    char stringdata13[11];
    char stringdata14[9];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_TradeRecordModel_t::offsetsAndSizes) + ofs), len 
static const qt_meta_stringdata_TradeRecordModel_t qt_meta_stringdata_TradeRecordModel = {
    {
        QT_MOC_LITERAL(0, 16),  // "TradeRecordModel"
        QT_MOC_LITERAL(17, 12),  // "countChanged"
        QT_MOC_LITERAL(30, 0),  // ""
        QT_MOC_LITERAL(31, 8),  // "addTrade"
        QT_MOC_LITERAL(40, 6),  // "symbol"
        QT_MOC_LITERAL(47, 5),  // "price"
        QT_MOC_LITERAL(53, 6),  // "volume"
        QT_MOC_LITERAL(60, 4),  // "side"
        QT_MOC_LITERAL(65, 5),  // "clear"
        QT_MOC_LITERAL(71, 9),  // "RoleNames"
        QT_MOC_LITERAL(81, 8),  // "TimeRole"
        QT_MOC_LITERAL(90, 10),  // "SymbolRole"
        QT_MOC_LITERAL(101, 9),  // "PriceRole"
        QT_MOC_LITERAL(111, 10),  // "VolumeRole"
        QT_MOC_LITERAL(122, 8)   // "SideRole"
    },
    "TradeRecordModel",
    "countChanged",
    "",
    "addTrade",
    "symbol",
    "price",
    "volume",
    "side",
    "clear",
    "RoleNames",
    "TimeRole",
    "SymbolRole",
    "PriceRole",
    "VolumeRole",
    "SideRole"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_TradeRecordModel[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       1,   43, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   32,    2, 0x06,    1 /* Public */,

 // methods: name, argc, parameters, tag, flags, initial metatype offsets
       3,    4,   33,    2, 0x02,    2 /* Public */,
       8,    0,   42,    2, 0x02,    7 /* Public */,

 // signals: parameters
    QMetaType::Void,

 // methods: parameters
    QMetaType::Void, QMetaType::QString, QMetaType::Double, QMetaType::Double, QMetaType::QString,    4,    5,    6,    7,
    QMetaType::Void,

 // enums: name, alias, flags, count, data
       9,    9, 0x0,    5,   48,

 // enum data: key, value
      10, uint(TradeRecordModel::TimeRole),
      11, uint(TradeRecordModel::SymbolRole),
      12, uint(TradeRecordModel::PriceRole),
      13, uint(TradeRecordModel::VolumeRole),
      14, uint(TradeRecordModel::SideRole),

       0        // eod
};

void TradeRecordModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TradeRecordModel *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->countChanged(); break;
        case 1: _t->addTrade((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[4]))); break;
        case 2: _t->clear(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (TradeRecordModel::*)();
            if (_t _q_method = &TradeRecordModel::countChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject TradeRecordModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractListModel::staticMetaObject>(),
    qt_meta_stringdata_TradeRecordModel.offsetsAndSizes,
    qt_meta_data_TradeRecordModel,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_TradeRecordModel_t
, QtPrivate::TypeAndForceComplete<TradeRecordModel, std::true_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>

, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<double, std::false_type>, QtPrivate::TypeAndForceComplete<double, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>

>,
    nullptr
} };


const QMetaObject *TradeRecordModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TradeRecordModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_TradeRecordModel.stringdata0))
        return static_cast<void*>(this);
    return QAbstractListModel::qt_metacast(_clname);
}

int TradeRecordModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractListModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void TradeRecordModel::countChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE

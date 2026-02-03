/****************************************************************************
** Meta object code from reading C++ file 'GlobalState.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.3.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../include/GlobalState.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'GlobalState.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.3.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_GlobalState_t {
    uint offsetsAndSizes[34];
    char stringdata0[12];
    char stringdata1[23];
    char stringdata2[1];
    char stringdata3[6];
    char stringdata4[13];
    char stringdata5[17];
    char stringdata6[18];
    char stringdata7[18];
    char stringdata8[19];
    char stringdata9[20];
    char stringdata10[16];
    char stringdata11[6];
    char stringdata12[10];
    char stringdata13[11];
    char stringdata14[11];
    char stringdata15[12];
    char stringdata16[13];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_GlobalState_t::offsetsAndSizes) + ofs), len 
static const qt_meta_stringdata_GlobalState_t qt_meta_stringdata_GlobalState = {
    {
        QT_MOC_LITERAL(0, 11),  // "GlobalState"
        QT_MOC_LITERAL(12, 22),  // "usePreciseMatchChanged"
        QT_MOC_LITERAL(35, 0),  // ""
        QT_MOC_LITERAL(36, 5),  // "value"
        QT_MOC_LITERAL(42, 12),  // "tokenChanged"
        QT_MOC_LITERAL(55, 16),  // "accountIdChanged"
        QT_MOC_LITERAL(72, 17),  // "jqUsernameChanged"
        QT_MOC_LITERAL(90, 17),  // "jqPasswordChanged"
        QT_MOC_LITERAL(108, 18),  // "jqConnectedChanged"
        QT_MOC_LITERAL(127, 19),  // "jqConnectingChanged"
        QT_MOC_LITERAL(147, 15),  // "usePreciseMatch"
        QT_MOC_LITERAL(163, 5),  // "token"
        QT_MOC_LITERAL(169, 9),  // "accountId"
        QT_MOC_LITERAL(179, 10),  // "jqUsername"
        QT_MOC_LITERAL(190, 10),  // "jqPassword"
        QT_MOC_LITERAL(201, 11),  // "jqConnected"
        QT_MOC_LITERAL(213, 12)   // "jqConnecting"
    },
    "GlobalState",
    "usePreciseMatchChanged",
    "",
    "value",
    "tokenChanged",
    "accountIdChanged",
    "jqUsernameChanged",
    "jqPasswordChanged",
    "jqConnectedChanged",
    "jqConnectingChanged",
    "usePreciseMatch",
    "token",
    "accountId",
    "jqUsername",
    "jqPassword",
    "jqConnected",
    "jqConnecting"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_GlobalState[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       7,   77, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   56,    2, 0x06,    8 /* Public */,
       4,    1,   59,    2, 0x06,   10 /* Public */,
       5,    1,   62,    2, 0x06,   12 /* Public */,
       6,    1,   65,    2, 0x06,   14 /* Public */,
       7,    1,   68,    2, 0x06,   16 /* Public */,
       8,    1,   71,    2, 0x06,   18 /* Public */,
       9,    1,   74,    2, 0x06,   20 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Bool,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Bool,    3,
    QMetaType::Void, QMetaType::Bool,    3,

 // properties: name, type, flags
      10, QMetaType::Bool, 0x00015103, uint(0), 0,
      11, QMetaType::QString, 0x00015103, uint(1), 0,
      12, QMetaType::QString, 0x00015103, uint(2), 0,
      13, QMetaType::QString, 0x00015103, uint(3), 0,
      14, QMetaType::QString, 0x00015103, uint(4), 0,
      15, QMetaType::Bool, 0x00015103, uint(5), 0,
      16, QMetaType::Bool, 0x00015103, uint(6), 0,

       0        // eod
};

void GlobalState::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<GlobalState *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->usePreciseMatchChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->tokenChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->accountIdChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->jqUsernameChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->jqPasswordChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->jqConnectedChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 6: _t->jqConnectingChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (GlobalState::*)(bool );
            if (_t _q_method = &GlobalState::usePreciseMatchChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (GlobalState::*)(const QString & );
            if (_t _q_method = &GlobalState::tokenChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (GlobalState::*)(const QString & );
            if (_t _q_method = &GlobalState::accountIdChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (GlobalState::*)(const QString & );
            if (_t _q_method = &GlobalState::jqUsernameChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (GlobalState::*)(const QString & );
            if (_t _q_method = &GlobalState::jqPasswordChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (GlobalState::*)(bool );
            if (_t _q_method = &GlobalState::jqConnectedChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (GlobalState::*)(bool );
            if (_t _q_method = &GlobalState::jqConnectingChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<GlobalState *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< bool*>(_v) = _t->usePreciseMatch(); break;
        case 1: *reinterpret_cast< QString*>(_v) = _t->token(); break;
        case 2: *reinterpret_cast< QString*>(_v) = _t->accountId(); break;
        case 3: *reinterpret_cast< QString*>(_v) = _t->jqUsername(); break;
        case 4: *reinterpret_cast< QString*>(_v) = _t->jqPassword(); break;
        case 5: *reinterpret_cast< bool*>(_v) = _t->jqConnected(); break;
        case 6: *reinterpret_cast< bool*>(_v) = _t->jqConnecting(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<GlobalState *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setUsePreciseMatch(*reinterpret_cast< bool*>(_v)); break;
        case 1: _t->setToken(*reinterpret_cast< QString*>(_v)); break;
        case 2: _t->setAccountId(*reinterpret_cast< QString*>(_v)); break;
        case 3: _t->setJqUsername(*reinterpret_cast< QString*>(_v)); break;
        case 4: _t->setJqPassword(*reinterpret_cast< QString*>(_v)); break;
        case 5: _t->setJqConnected(*reinterpret_cast< bool*>(_v)); break;
        case 6: _t->setJqConnecting(*reinterpret_cast< bool*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    } else if (_c == QMetaObject::BindableProperty) {
    }
#endif // QT_NO_PROPERTIES
}

const QMetaObject GlobalState::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_GlobalState.offsetsAndSizes,
    qt_meta_data_GlobalState,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_GlobalState_t
, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<QString, std::true_type>, QtPrivate::TypeAndForceComplete<QString, std::true_type>, QtPrivate::TypeAndForceComplete<QString, std::true_type>, QtPrivate::TypeAndForceComplete<QString, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<GlobalState, std::true_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>



>,
    nullptr
} };


const QMetaObject *GlobalState::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *GlobalState::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_GlobalState.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int GlobalState::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void GlobalState::usePreciseMatchChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void GlobalState::tokenChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void GlobalState::accountIdChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void GlobalState::jqUsernameChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void GlobalState::jqPasswordChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void GlobalState::jqConnectedChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void GlobalState::jqConnectingChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE

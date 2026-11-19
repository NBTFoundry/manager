#ifndef NBT_PLUGIN_FRAMEWORK_HPP
#define NBT_PLUGIN_FRAMEWORK_HPP
namespace bmw {
class IDispatcher;
class Bundle;
class Runnable;
class Parcel {
public:
    void readBool(bool &value) const; // _ZNK3bmw6Parcel8readBoolERb
};
enum RHMIInterfaceVersion { RHMI_INTERFACE_VERSION_1 = 1 };
template <typename T> class BasicString {
public:
    explicit BasicString(const T *text);
    BasicString(const BasicString &other);
    ~BasicString();
    const T *data() const {
        return data_;
    }

private:
    T *data_;
};
template <typename R, typename A1, typename A2, typename A3, typename A4> struct Delegate {
    void *object;
    R (*stub)(void *, A1);
};
typedef Delegate<bool, const Parcel &, void, void, void> ActionDelegate;
typedef Delegate<bool, const BasicString<unsigned short> &, void, void, void> CdsDelegate;

class PluginApp {
public:
    PluginApp(IDispatcher &, BasicString<char>, unsigned, unsigned, RHMIInterfaceVersion,
              const Bundle &, const Bundle &);
    void addHandler(unsigned, const ActionDelegate &);
    void addHandler(unsigned, unsigned, const ActionDelegate &);
    void triggerEvent(unsigned);
    void post(Runnable *, unsigned);
    void cdsAddPropertyChangedEventHandler(BasicString<char> &, unsigned, const CdsDelegate &);
    void cdsGetPropertyAsync(BasicString<char> &, const CdsDelegate &);
    void cdsRemovePropertyChangedEventHandler(BasicString<char> &);
    bool onInit();
    void onFinalize();
};
} // namespace bmw
#endif

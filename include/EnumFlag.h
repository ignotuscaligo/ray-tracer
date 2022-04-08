#define ENUM_FLAG(Enum, Enum_t) \
enum class Enum; \
inline Enum operator&(Enum a, Enum b) { return static_cast<Enum>(static_cast<Enum_t>(a) & static_cast<Enum_t>(b)); } \
inline Enum operator|(Enum a, Enum b) { return static_cast<Enum>(static_cast<Enum_t>(a) | static_cast<Enum_t>(b)); } \
inline Enum operator^(Enum a, Enum b) { return static_cast<Enum>(static_cast<Enum_t>(a) ^ static_cast<Enum_t>(b)); } \
inline Enum operator~(Enum a) { return static_cast<Enum>(~static_cast<Enum_t>(a)); } \
inline Enum& operator&=(Enum& a, Enum b) { a = a & b; return a; } \
inline Enum& operator|=(Enum& a, Enum b) { a = a | b; return a; } \
inline Enum& operator^=(Enum& a, Enum b) { a = a ^ b; return a; } \
inline bool checkFlag(Enum a) { return static_cast<Enum_t>(a) != 0; }

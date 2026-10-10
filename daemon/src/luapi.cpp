#include <lua.hpp>

#include <meta>
#include <string>

namespace lua::bindings
{
    static int GetVersion(lua_State* L)
    {
        lua_pushnumber(L, lua_version(L));
        return 1;
    }

    static int Launch(lua_State* L)
    {

        return 0;
    }

    static int Kill(lua_State* L)
    {

        return 0;
    }
}

template <auto Member>
constexpr auto bind_member(lua_State* L)
{
    if constexpr (!is_special_member_function(Member)) {
        lua_pushcfunction(L, &[:Member:]);
        std::string name(display_string_of(Member));
        lua_setglobal(L, name.c_str());
    }
}

template <auto Type>
constexpr auto bind(lua_State* L)
{
    static constexpr auto ctx = std::meta::access_context::unprivileged();
    template for (constexpr auto mem : define_static_array(members_of(Type, ctx))) {
        bind_member<mem>(L);
    }
}

void RegisterLuaFunctions(lua_State* L)
{
    if(L == nullptr) return;
    bind<^^lua::bindings>(L);
}

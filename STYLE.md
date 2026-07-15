# style

Classes in an anonymous namespace in a .cpp declare their constructors,
destructor and methods in-class only; definitions go out of line, below and
outside the `namespace {}`, at file scope:

```cpp
namespace {
    struct FooImpl: public Foo {
        FooImpl(int x);
        void bar() override;
    };
}

void FooImpl::bar() {
    ...
}
```

Constructors: signature on its own line, each initializer on its own line
(`:` first, `,` after), opening brace on its own line:

```cpp
IconStoreImpl(struct ev_loop* l, IconPool& p)
    : loop(l)
    , icons(&p)
{
    ...
}
```

Strings: owned strings are `stl::StringBuilder` (pool objects may hold them —
`ObjList::make` placement-news, `release` destructs). No fixed `char buf[N]`
as string storage and no hand-rolled copy helpers. Fixed arrays are fine for
what they really are: stack IO scratch, bounded wire tokens in trivial
`stl::Vector` elements, rings/bitmaps/index tables, and imgui InputText
buffers (its API contract).

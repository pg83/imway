# style

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

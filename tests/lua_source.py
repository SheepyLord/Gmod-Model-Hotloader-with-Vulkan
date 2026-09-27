"""Pieces of addon Lua for harnesses that run them outside the game."""


def definition(lua, source, header):
    """The definition starting at header (a function, or a statement such as
    hook.Add(...)): the fewest whole lines from it that compile on their own,
    whatever the file places after it."""
    compiles = lua.eval('function(s) return load(s)~=nil end')
    start = end = source.index(header)
    while end < len(source):
        end = source.find('\n', end) + 1 or len(source)
        if compiles(source[start:end]): return source[start:end]
    raise AssertionError(header + ' has no complete definition')

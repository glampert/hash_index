
## hash_index

A fast hash table template for array indexes that keeps the hash keys separated from the mapped values.

----

`hash_index` is based on a similar class used by the DOOM 3 game from id Software.
I have adapted it to be usable as a standalone template class and also added a few
extra customization options and additional helper methods. You can find the original
`idHashIndex` class [in here](https://github.com/id-Software/DOOM-3-BFG/blob/master/neo/idlib/containers/HashIndex.h).

A basic usage example:

```cpp
#include "hash_index.hpp"
#include <cassert>
#include <string>
#include <vector>

struct Player
{
    std::string name;
    int level;
    int health;
    // ...
};

class PlayerRegistry
{
public:
    const Player * findPlayerByName(const std::string & name) const
    {
        const auto p_index = hash_idx.find(name_hasher(name), name, player_list,
                                           [](const std::string & name, const Player & player)
                                           {
                                               return name == player.name;
                                           });
        if (p_index == hash_idx.null_index)
        {
            return nullptr;
        }
        return &player_list[p_index];
    }

    void addPlayer(std::string name, const int lvl, const int hp)
    {
        hash_idx.insert(name_hasher(name), player_list.size());
        player_list.push_back({ std::move(name), lvl, hp });
    }

private:
    hash_index<>           hash_idx;
    std::vector<Player>    player_list;
    std::hash<std::string> name_hasher;
};

int main()
{
    PlayerRegistry player_reg;

    player_reg.addPlayer("Timmy",    10, 100);
    player_reg.addPlayer("K1ll3r",   63, 97 );
    player_reg.addPlayer("1337john", 82, 100);

    assert(player_reg.findPlayerByName("Timmy")    != nullptr);
    assert(player_reg.findPlayerByName("K1ll3r")   != nullptr);
    assert(player_reg.findPlayerByName("1337john") != nullptr);

    assert(player_reg.findPlayerByName("bobby")  == nullptr);
    assert(player_reg.findPlayerByName("johnny") == nullptr);
}
```

You can find more documentation in the `hash_index.hpp` header file.

### License

`hash_index` is released under the terms of the GPL version 3
to comply with the original license used by the code this was based on.


-- Monster placement tests.

local place = dgn.point(20, 20)

local function place_monster_on(x, y, monster, feature)
  dgn.grid(place.x, place.y, feature)
  return dgn.create_monster(x, y, monster)
end

local function assert_place_monster_on(monster, feature)
  feature = feature or 'floor'
  dgn.dismiss_monsters()
  crawl.message("Placing " .. monster .. " on " .. feature)
  assert(place_monster_on(place.x, place.y, monster, feature),
         "Could not place monster " .. monster .. " on " .. feature)

  if monster ~= 'mimic' then
    local realname = dgn.mons_at(place.x, place.y).name
    assert(realname == monster,
           "Monster placed is '" .. realname ..
             "', expected '" .. monster .. "'")
  end
end

assert_place_monster_on("quokka", "floor")
assert_place_monster_on("necrophage", "altar_zin")
assert_place_monster_on("rat", "shallow water")

-- [ds] One wonders why due has this morbid fetish involving flying
-- skulls and lava...
assert_place_monster_on("flying skull", "lava")
assert_place_monster_on("rock worm", "rock_wall")
assert_place_monster_on("cyan ugly thing")
assert_place_monster_on("purple very ugly thing")

for i = 1, 100 do
  assert_place_monster_on("mimic")
end

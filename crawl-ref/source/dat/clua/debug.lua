------------------------------------------------------------------------------
-- debug.lua
-- Debugging functions to call from the wizard lua interpreter.
------------------------------------------------------------------------------

require('clua/userbase.lua')

function debug_wrath()
  debug.never_die(true)

  while crawl.kbhit() == 0 do
    -- Remove non-near monsters if menv[] full.
    debug.cull_monsters()

    -- Dismiss adjacent monsters to make room for wrath sending in
    -- more monsters.
    debug.dismiss_adjacent()

    -- Draw the wrath card
    crawl.process_keys("&cwrath\r")

    -- Redraw screen.
    crawl.redraw_view()
    crawl.redraw_stats()
  end

  crawl.flush_input()
  debug.never_die(false)
end
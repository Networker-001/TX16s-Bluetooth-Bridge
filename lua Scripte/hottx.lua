-- V17: S.Port Mirror (Special Function)
-- Ziel: Sendet Daten an UART2, wenn Schalter aktiv ist

local tick = 0
local lowPrioIdx = 1

-- Initialisierung beim Schalter-Umlegen
local function init()
  -- Weckt den Port auf
  serialWrite("") 
end

-- Hauptschleife (läuft nur bei Schalter AN)
local function run(event)
  -- Prio 1: Wichtige Daten (50% Sendezeit)
  local highPrio = {
    { name = "VFAS", id = 0x0210, factor = 100 },
    { name = "RSSI", id = 0xF101, factor = 1 }
  }
  
  -- Prio 2: Restliche MPM-Daten (im Wechsel)
  local lowPrio = {
    { name = "VBat", id = 0x0010, factor = 100 },
    { name = "Curr", id = 0x0200, factor = 10 },
    { name = "Capa", id = 0x0600, factor = 1 },
    { name = "Alt",  id = 0x0100, factor = 100 },
    { name = "VSpd", id = 0x0110, factor = 100 },
    { name = "GSpd", id = 0x0830, factor = 1000 },
    { name = "Sats", id = 0x0400, factor = 1 },
    { name = "Hdg",  id = 0x0840, factor = 100 },
    { name = "RPM",  id = 0x0500, factor = 1 }
  }

  local s = nil
  if tick % 2 == 0 then
    s = highPrio[(tick / 2 % #highPrio) + 1]
  else
    s = lowPrio[lowPrioIdx]
    lowPrioIdx = (lowPrioIdx % #lowPrio) + 1
  end

  local val = getValue(s.name)
  
  if val ~= nil then
    -- Regelkonforme FrSky Skalierung
    local d = math.floor(val * s.factor)
    local d1, d2, d3, d4 = d%256, math.floor(d/256)%256, math.floor(d/65536)%256, math.floor(d/16777216)%256
    local idL, idH = s.id % 256, math.floor(s.id / 256)
    local chk = (idL + idH + d1 + d2 + d3 + d4) % 256
    
    -- Paket als 10-Byte Block senden
    serialWrite(string.char(0x4D, 0x50, 0x06, idL, idH, d1, d2, d3, d4, chk))
  end

  tick = tick + 1
  return 0
end

-- Rückgabe der Funktionen an EdgeTX
return { init=init, run=run }
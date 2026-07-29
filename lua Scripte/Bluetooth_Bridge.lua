-- ==============================================================================
-- --- BLUETOOTH BRIDGE CONFIGURATION FOR EDGETX ---
-- Version: 2.3 (Mit Auto-Power AUX2 & UART2-Warnhinweis im Header)
-- ==============================================================================

local script_title = "BT Bridge Config V2.3"
local selected_item = 1
local edit_mode = false      
local current_editor = ""    
local pin_cursor = 1    
local name_cursor = 1        

local config_path = "/SCRIPTS/TELEMETRY/bt_bridge_cfg.txt"

local char_table = {"A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","0","1","2","3","4","5","6","7","8","9","_","<"}

local orig_mode  = "BLE"          
local orig_pin   = {1, 2, 3, 4}   
local orig_name  = "TX16S"
local orig_debug = "AUS"

local edit_mode_val = "BLE"       
local edit_pin      = {1, 2, 3, 4}
local edit_name_tbl = {"T","X","1","6","S","<","<","<"} 
local edit_debug    = "AUS"

local status_message = "Konfiguration bereit"

local function getNameString()
    local str = ""
    for i = 1, 8 do
        if edit_name_tbl[i] == "<" then
            break
        elseif edit_name_tbl[i] == " " then
            str = str .. "_" 
        else
            str = str .. edit_name_tbl[i]
        end
    end
    if str == "" then str = "TX16S" end 
    return str
end

local function getCharIndex(char)
    for i, c in ipairs(char_table) do
        if c == char then return i end
    end
    return 1
end

local function saveConfiguration()
    local f = io.open(config_path, "w")
    if f then
        io.write(f, "mode=" .. edit_mode_val .. "\n")
        io.write(f, "pin=" .. table.concat(edit_pin, ",") .. "\n")
        io.write(f, "name=" .. getNameString() .. "\n")
        io.write(f, "debug=" .. edit_debug .. "\n")
        io.close(f)
        
        orig_mode = edit_mode_val
        orig_pin = {edit_pin, edit_pin, edit_pin, edit_pin}
        orig_name = getNameString()
        orig_debug = edit_debug
        
        status_message = "Konfiguration gespeichert!"
        return true
    else
        status_message = "Fehler: Speichern fehlgeschlagen"
        return false
    end
end

local function loadConfiguration()
    local f = io.open(config_path, "r")
    if not f then
        status_message = "Standardwerte geladen (keine Datei)"
        return false
    end

    local content = io.read(f, 500)
    io.close(f)

    if content then
        for line in string.gmatch(content, "[^\r\n]+") do
            local key, value = string.match(line, "([^=]+)=([^=]+)")
            if key and value then
                if key == "mode" then
                    edit_mode_val = value
                    orig_mode = value
                elseif key == "debug" then
                    edit_debug = value
                    orig_debug = value
                elseif key == "name" then
                    orig_name = value
                    for i = 1, 8 do
                        local c = string.sub(value, i, i)
                        if c == " " then c = "_" end
                        edit_name_tbl[i] = (c ~= "") and c or "<"
                    end
                elseif key == "pin" then
                    local p1, p2, p3, p4 = string.match(value, "(%d),(%d),(%d),(%d)")
                    if p1 and p2 and p3 and p4 then
                        edit_pin = {tonumber(p1), tonumber(p2), tonumber(p3), tonumber(p4)}
                        orig_pin = {tonumber(p1), tonumber(p2), tonumber(p3), tonumber(p4)}
                    end
                end
            end
        end
        status_message = "Konfiguration von SD geladen"
        return true
    end
    return false
end

local function sendShortCommand(cmd_type, cmd_value)
    local safe_value = math.floor(tonumber(cmd_value) or 0) % 256
    serialWrite(string.char(0xAA, 0xBB, cmd_type, safe_value))
    return true
end

local function sendNameCommand(name_text)
    local text_len = string.len(name_text)
    serialWrite(string.char(0xAA, 0xBB, 0x04, text_len))
    serialWrite(name_text)
    return true
end

local function init()
    serialWrite("") 
    -- FIX: Aktiviert beim Skriptstart automatisch die Stromversorgung an AUX2 (Index 1)
    if serialSetPower then
        serialSetPower(1, 1) 
    end
    loadConfiguration() 
end


local function run(event)
    lcd.clear()
    lcd.drawFilledRectangle(0, 0, 480, 272, GREY)
    
    -- 1. HEADER ZEICHNEN (Mit integriertem orangenen Warnhinweis rechts)
    lcd.drawFilledRectangle(0, 0, 480, 36, BLUE)
    -- Titel nach links verschoben für Platz
    lcd.drawText(15, 10, script_title, WHITE) 
    -- Warntext oben rechts in Orange platziert
    lcd.drawText(465, 12, "Setze: UART2 -> LUA & POWER ON!", RIGHT + RED + SMLSIZE + INVERS)
    lcd.drawLine(0, 36, 480, 36, SOLID, WHITE)

    -- Geometrie für das 2x2 Raster (Untere Bildschirmhälfte)
    local btnW, btnH = 220, 32
    local row1Y, row2Y = 150, 188
    local col1X, col2X = 15, 245

    local trigger_action = 0

    -- ========================================================
    -- 2. SCROLLRAD-STEUERUNG
    -- ========================================================
    if not edit_mode then
        if event == 4100 then 
            selected_item = selected_item + 1
            if selected_item > 8 then selected_item = 1 end
        elseif event == 4099 then 
            selected_item = selected_item - 1
            if selected_item < 1 then selected_item = 8 end
        end

        if event == 514 then
            if selected_item <= 4 then
                if selected_item == 1 then 
                    edit_mode_val = (edit_mode_val == "BLE") and "Classic BT" or "BLE"
                    status_message = "Modus im RAM angepasst"
                elseif selected_item == 2 then 
                    edit_mode = true current_editor = "PIN" pin_cursor = 1
                    status_message = "PIN-Edit: Rad=Ziffer, PAGE=Wechseln"
                elseif selected_item == 3 then 
                    edit_mode = true current_editor = "NAME" name_cursor = 1
                    status_message = "Name-Edit: Rad=Buchstabe, PAGE=Wechseln"
                elseif selected_item == 4 then 
                    edit_debug = (edit_debug == "AUS") and "EIN" or "AUS"
                    status_message = "Debug im RAM angepasst"
                end
            else
                trigger_action = selected_item
            end
        end
    else
        -- Editor-Logik für PIN und Name
        if current_editor == "PIN" then
            if event == 4100 then
                edit_pin[pin_cursor] = edit_pin[pin_cursor] + 1
                if edit_pin[pin_cursor] > 9 then edit_pin[pin_cursor] = 0 end
            elseif event == 4099 then
                edit_pin[pin_cursor] = edit_pin[pin_cursor] - 1
                if edit_pin[pin_cursor] < 0 then edit_pin[pin_cursor] = 9 end
            end
            if event == 516 or event == EVT_PAGE_BREAK or event == EVT_PAGE_NEXT then
                pin_cursor = pin_cursor + 1
                if pin_cursor > 4 then pin_cursor = 1 end
            elseif event == 515 then
                pin_cursor = pin_cursor - 1
                if pin_cursor < 1 then pin_cursor = 4 end
            end
            if event == 514 then edit_mode = false saveConfiguration() status_message = "PIN gesichert." end
        elseif current_editor == "NAME" then
            local idx = getCharIndex(edit_name_tbl[name_cursor])
            if event == 4100 then 
                idx = idx + 1 if idx > #char_table then idx = 1 end
                edit_name_tbl[name_cursor] = char_table[idx]
            elseif event == 4099 then 
                idx = idx - 1 if idx < 1 then idx = #char_table end
                edit_name_tbl[name_cursor] = char_table[idx]
            end
            if event == 516 or event == EVT_PAGE_BREAK or event == EVT_PAGE_NEXT or event == EVT_PAGE_LONG or event == EVT_PAGEDN_BREAK then 
                name_cursor = name_cursor + 1 if name_cursor > 8 then name_cursor = 1 end
            elseif event == 515 then 
                name_cursor = name_cursor - 1 if name_cursor < 1 then name_cursor = 8 end
            end
            if event == 514 then edit_mode = false saveConfiguration() status_message = "Name gesichert." end
        end
    end

    -- ========================================================
    -- 3. AKTIONEN AUSFÜHREN
    -- ========================================================
    if trigger_action == 5 then
        for i = 1, 8 do edit_name_tbl[i] = "<" end
        status_message = "Textfeld geloescht!"
    elseif trigger_action == 6 then
        sendShortCommand(0x03, 1)
        status_message = "Koppel-Reset an Box gesendet!"
    elseif trigger_action == 8 then
        edit_mode_val = orig_mode edit_debug = orig_debug
        for i = 1, 4 do edit_pin[i] = orig_pin[i] end
        for i = 1, 8 do
            local c = string.sub(orig_name, i, i)
            if c == " " then c = "_" end
            edit_name_tbl[i] = (c ~= "") and c or "<"
        end
        status_message = "Änderungen verworfen!"
    elseif trigger_action == 7 then
        status_message = "Pruefe Aenderungen..."
        local sent_anything = false
        if edit_mode_val ~= orig_mode then
            sendShortCommand(0x01, (edit_mode_val == "BLE") and 1 or 0)
            orig_mode = edit_mode_val sent_anything = true
        end
                local pin_changed = false
                for i = 1, 4 do if edit_pin[i] ~= orig_pin[i] then pin_changed = true end end
                if pin_changed then
                    -- FIX: Jeder einzelne Index der PIN-Tabelle wird jetzt mathematisch korrekt multipliziert!
                    local num_pin = (edit_pin[1] * 1000) + (edit_pin[2] * 100) + (edit_pin[3] * 10) + edit_pin[4]
                    sendShortCommand(0x02, num_pin % 256)
                    for i = 1, 4 do orig_pin[i] = edit_pin[i] end sent_anything = true
                end

        if edit_debug ~= orig_debug then
            sendShortCommand(0x05, (edit_debug == "EIN") and 1 or 0)
            orig_debug = edit_debug sent_anything = true
        end
        local current_name_str = getNameString()
        if current_name_str ~= orig_name then
            sendNameCommand(current_name_str)
            orig_name = current_name_str sent_anything = true
        end
        if sent_anything then saveConfiguration() status_message = "Gesendet & gespeichert!"
        else status_message = "Keine Aenderungen vorhanden!" end
    end

    -- ========================================================
    -- 4. BENUTZEROBERFLÄCHE ZEICHNEN
    -- ========================================================
    local startY, stepY = 44, 22

    for i = 1, 4 do
        if selected_item == i then
            lcd.drawFilledRectangle(10, startY + (i - 1) * stepY - 2, 460, 20, BLACK)
        end
    end

    lcd.drawText(20, startY, "BT Modus: " .. edit_mode_val, YELLOW)
    
    local pin_str = ""
    for i = 1, 4 do
        if edit_mode and current_editor == "PIN" and pin_cursor == i then pin_str = pin_str .. "[" .. tostring(edit_pin[i]) .. "]"
        else pin_str = pin_str .. tostring(edit_pin[i]) end
    end
    lcd.drawText(20, startY + stepY, "Sicherheits-PIN: " .. pin_str, YELLOW)
    
    local name_display = ""
    for i = 1, 8 do
        if edit_mode and current_editor == "NAME" then
            if name_cursor == i then name_display = name_display .. "[" .. edit_name_tbl[i] .. "]"
            else name_display = name_display .. (edit_name_tbl[i] == "<" and "_" or edit_name_tbl[i]) end
        else
            if edit_name_tbl[i] ~= "<" then name_display = name_display .. edit_name_tbl[i] end
        end
    end
    if name_display == "" and not edit_mode then name_display = "(leer)" end
    lcd.drawText(20, startY + (2 * stepY), "Bluetooth Name: " .. name_display, YELLOW)
    lcd.drawText(20, startY + (3 * stepY), "Telemetrie-Debug: " .. edit_debug, YELLOW)

    -- --- 2x2 BUTTON GRID ZEICHNEN ---
    local function drawGridButton(x, y, w, h, text, item_idx)
        local is_selected = (selected_item == item_idx)
        
        -- Zeichnet den Rahmen
        lcd.drawRectangle(x, y, w, h, SOLID, 1)
        
        -- Hintergrund: SCHWARZ wenn ausgewählt, sonst GRAU
        lcd.drawFilledRectangle(x + 2, y + 2, w - 4, h - 4, is_selected and BLACK or GREY)
        
        -- FIX: Der Text wird jetzt IMMER gezeichnet (leuchtend GELB auf schwarzem/grauem Grund)
        lcd.drawText(x + (w / 2), y + (h / 2) - 8, text, CENTER + YELLOW)
    end


    drawGridButton(col1X, row1Y, btnW, btnH, "Textfeld leeren", 5)
    drawGridButton(col2X, row1Y, btnW, btnH, "Hardware Koppel-Reset", 6)
    drawGridButton(col1X, row2Y, btnW, btnH, "Senden und Speichern", 7)
    drawGridButton(col2X, row2Y, btnW, btnH, "Abbrechen / Reset", 8)

    -- ========================================================
    -- 5. STATUSBAR GANZ UNTEN (Orangefarbener Text)
    -- ========================================================
    lcd.drawLine(0, 234, 480, 234, SOLID, WHITE)
    lcd.drawFilledRectangle(0, 235, 480, 37, GREY)
    lcd.drawText(15, 243, "Status: " .. status_message, ORANGE + SMLSIZE)

    return 0
end

return { run=run, init=init }

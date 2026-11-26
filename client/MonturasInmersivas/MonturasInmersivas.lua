local prefix = "MonturaCPP"
local function SendCommand(command)
    SendAddonMessage(prefix, command, "WHISPER", UnitName("player"))
end

local parkedId, activeMountSpell, activeBonus
local fallbackFoodIcon = "Interface\\Icons\\INV_Misc_Food_19"

local moodIcon = CreateFrame("Button", nil, TargetFrame)
moodIcon:SetWidth(22)
moodIcon:SetHeight(22)
moodIcon:SetFrameLevel(TargetFrame:GetFrameLevel() + 2)
moodIcon:SetPoint("LEFT", TargetFramePortrait, "RIGHT", 3, 19)
local moodTexture = moodIcon:CreateTexture(nil, "ARTWORK")
moodTexture:SetAllPoints(moodIcon)
moodTexture:SetTexture("Interface\\PetPaperDollFrame\\UI-PetHappiness")
moodIcon:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText(self.mood, self.red, self.green, self.blue)
    GameTooltip:AddLine(("Felicidad: %d%%"):format(self.happiness), self.red, self.green, self.blue)
    if self.maxBonus then
        GameTooltip:AddLine(("Velocidad: +%d%% (máx. +%d%%)"):format(self.speed * self.maxBonus / 100, self.maxBonus), 1, 1, 1)
    else
        GameTooltip:AddLine(("Velocidad: %d%% de la máxima"):format(self.speed), 1, 1, 1)
    end
    GameTooltip:Show()
end)
moodIcon:SetScript("OnLeave", function() GameTooltip:Hide() end)
moodIcon:Hide()

local foodButton = CreateFrame("Button", nil, moodIcon)
foodButton:SetWidth(19)
foodButton:SetHeight(19)
foodButton:SetPoint("TOP", moodIcon, "BOTTOM", 0, -3)
foodButton:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
foodButton:SetNormalTexture(fallbackFoodIcon)
foodButton:SetScript("OnClick", function(self)
    if self.canFeed then SendCommand("F" .. self.foodId) end
end)
foodButton:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText(self.foodId > 0 and (GetItemInfo(self.foodId) or "Comida") or "Alimentar montura")
    GameTooltip:AddLine(self.hint, 1, 1, 1)
    GameTooltip:Show()
end)
foodButton:SetScript("OnLeave", function() GameTooltip:Hide() end)

local leashButton = CreateFrame("Button", nil, moodIcon)
leashButton:SetWidth(19)
leashButton:SetHeight(19)
leashButton:SetPoint("TOP", foodButton, "BOTTOM", 0, -3)
leashButton:SetNormalTexture("Interface\\Icons\\INV_Misc_Bandage_15")
leashButton:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
leashButton:SetScript("OnClick", function()
    if parkedId then SendCommand(leashButton.leading and "L0" or "L1") end
end)
local function showLeashTip(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText(self.leading and "Dejar montura aquí" or "Llevar montura")
    GameTooltip:Show()
end
leashButton:SetScript("OnEnter", showLeashTip)
leashButton:SetScript("OnLeave", function() GameTooltip:Hide() end)

local channel = CreateFrame("StatusBar", nil, moodIcon)
channel:SetWidth(165)
channel:SetHeight(12)
channel:SetPoint("TOPRIGHT", TargetFramePortrait, "BOTTOMRIGHT", 0, -9)
channel:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
channel:SetStatusBarColor(0.25, 0.72, 0.30)
local channelText = channel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
channelText:SetPoint("CENTER")
channelText:SetText("Alimentando…")
channel:SetScript("OnUpdate", function(self)
    local left = math.max(0, self.finish - GetTime())
    self:SetValue(left)
    if left == 0 then self:Hide() end
end)
channel:Hide()

local function UpdateTarget()
    local guid = UnitGUID("target")
    if parkedId and parkedId > 0 and guid and tonumber(guid:sub(-6), 16) == parkedId
        and tonumber(guid:sub(-12, -7), 16) == 901087 then
        moodIcon:Show()
    else
        moodIcon:Hide()
    end
end

moodIcon:RegisterEvent("PLAYER_TARGET_CHANGED")
moodIcon:SetScript("OnEvent", UpdateTarget)

local function SetStatus(_, status, mood, happiness, speed, action, foodId, remaining, duration, maxBonus, mountSpell, mountId, leading, _secondsLeft, _combatWait, injured)
    activeMountSpell = mountSpell
    activeBonus = maxBonus and speed * maxBonus / 100
    parkedId = mountId
    leashButton.leading = leading
    if GameTooltip:IsOwned(leashButton) then showLeashTip(leashButton) end
    local frame = happiness >= 70 and 0 or (happiness >= 35 and 0.1875 or 0.375)
    moodTexture:SetTexCoord(frame, frame + 0.1875, 0, 0.359375)
    local red, green, blue = 1, 0.50, 0.45
    if happiness >= 70 then
        red, green, blue = 0.55, 1, 0.55
    elseif happiness >= 35 then
        red, green, blue = 1, 0.84, 0.40
    end
    moodIcon.red, moodIcon.green, moodIcon.blue = red, green, blue
    moodIcon.mood, moodIcon.happiness, moodIcon.speed, moodIcon.maxBonus = mood, happiness, speed, maxBonus
    foodButton.foodId = foodId or 0
    foodButton.canFeed = action == "Entrar" and foodButton.foodId > 0 and not leading and (happiness < 100 or injured)
    foodButton.hint = foodButton.canFeed and "Clic para dar una porción."
        or (leading and "Suelta la correa para darle comida.")
        or (happiness >= 100 and not injured and "Tu montura está satisfecha."
        or (action ~= "Entrar" and "Alimentando a la montura." or "No tienes comida compatible."))
    local texture = foodButton.foodId > 0 and GetItemIcon and GetItemIcon(foodButton.foodId)
    foodButton:SetNormalTexture(texture or fallbackFoodIcon)
    foodButton:GetNormalTexture():SetDesaturated(not foodButton.canFeed)
    if remaining and remaining > 0 then
        channel:SetMinMaxValues(0, duration)
        channel.finish = GetTime() + remaining
        channel:SetValue(remaining)
        channel:Show()
    else
        channel:Hide()
    end
    UpdateTarget()
end

local function RefreshMountedBuff(tooltip, unit, index, filter)
    if not activeMountSpell or not activeBonus or not unit or not UnitIsUnit(unit, "player") then return end
    local name, _, _, _, _, _, _, _, _, _, spellId = UnitAura(unit, index, filter)
    if not name or (spellId and spellId ~= activeMountSpell)
        or (not spellId and name ~= GetSpellInfo(activeMountSpell)) then return end
    local lineText = ("Aumenta la velocidad un %d%%."):format(activeBonus)
    for line = 2, tooltip:NumLines() do
        local region = _G[tooltip:GetName() .. "TextLeft" .. line]
        local previous = region and region:GetText()
        if previous and (previous:lower():find("velocidad", 1, true) or previous:lower():find("speed", 1, true))
            and previous:find("%d+%%") then
            region:SetText(lineText)
            return
        end
    end
    tooltip:AddLine(lineText, 1, 1, 1)
    tooltip:Show()
end

if GameTooltip.SetUnitAura then hooksecurefunc(GameTooltip, "SetUnitAura", RefreshMountedBuff) end
if GameTooltip.SetUnitBuff then hooksecurefunc(GameTooltip, "SetUnitBuff", RefreshMountedBuff) end

local hoverKey, validUntil, nextQuery, changedCursor
local function hoveredHorse()
    if GetMouseFocus() ~= WorldFrame or IsMounted() or UnitAffectingCombat("player")
        or GetCursorInfo() or SpellIsTargeting() then return end
    local guid = UnitGUID("mouseover")
    if guid and tonumber(guid:sub(-12, -7), 16) == 901087 then
        return tonumber(guid:sub(-6), 16)
    end
end

local watcher = CreateFrame("Frame")
watcher:SetScript("OnUpdate", function()
    local key, now = hoveredHorse(), GetTime()
    if key ~= hoverKey then
        hoverKey, validUntil, nextQuery = key, nil, nil
    end
    if key and (not nextQuery or now >= nextQuery) then
        nextQuery = now + 0.65
        SendCommand("C" .. key)
    end
    if key and validUntil and now < validUntil then
        SetCursor("VEHICLE_CURSOR")
        changedCursor = true
    elseif changedCursor then
        ResetCursor()
        changedCursor = nil
    end
end)

local messages = CreateFrame("Frame")
messages:RegisterEvent("CHAT_MSG_ADDON")
messages:RegisterEvent("PLAYER_ENTERING_WORLD")
messages:SetScript("OnEvent", function(_, event, messagePrefix, payload, _, sender)
    if event == "PLAYER_ENTERING_WORLD" then
        SendCommand("S")
        return
    end
    if messagePrefix ~= prefix or not sender or sender:match("^[^-]+") ~= UnitName("player") then return end
    local key, allowed = payload:match("^C (%d+) ([01])$")
    if key then
        if tonumber(key) == hoverKey then validUntil = allowed == "1" and GetTime() + 1.2 or nil end
        return
    end
    local mountId, happiness, speed, foodId, leading, feeding, injured, mounted =
        payload:match("^S (%d+) (%d+) (%d+) (%d+) ([01]) ([01]) ([01]) ([01])$")
    if not mountId then return end
    mountId, happiness, speed, foodId = tonumber(mountId), tonumber(happiness), tonumber(speed), tonumber(foodId)
    local mood = happiness >= 70 and "Feliz" or (happiness >= 35 and "Contenta" or "Triste")
    local action = feeding == "1" and "Alimentando" or "Entrar"
    SetStatus(nil, nil, mood, happiness, speed, action, foodId, feeding == "1" and 3 or 0, 3, 100,
        mounted == "1" and 458 or nil, mountId, leading == "1", nil, nil, injured == "1")
end)

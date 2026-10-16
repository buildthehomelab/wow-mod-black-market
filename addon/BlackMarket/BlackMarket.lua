-- Black Market: the Mists of Pandaria Black Market Auction House window for 3.3.5a, backed by
-- the mod-black-market server module. Talk to the broker and the window opens: today's goods on
-- the left, the hot item on the right, a bid box at the bottom. Bids only, no buyouts.
--
-- The server and the addon talk over addon whispers to yourself with the prefix BMAH (see
-- src/BlackMarketAddon.cpp for the protocol). Built on Blizzard's own frames; DragonUI, when it
-- is installed, only reskins them.

local PREFIX = "BMAH"
local PROTOCOL = 1

local WIDTH, HEIGHT = 840, 490
local ROW_HEIGHT = 34
local ROWS = 9

-- Flags on a listing (src/BlackMarket.h ListingFlags).
local HIGH_BIDDER = 1
local HOT = 2
local ERA_LOCKED = 4
local LEVEL_LOCKED = 8
local KNOWN = 16

local TIME_LEFT = {
	[1] = { "Short", 1, 0.25, 0.25 },
	[2] = { "Medium", 1, 0.6, 0.2 },
	[3] = { "Long", 1, 0.82, 0 },
	[4] = { "Very Long", 0.9, 0.9, 0.9 },
}

local floor = math.floor
local band = bit.band

local listings, incoming = {}, nil
local minLevel = 0
local selected
local frame

-----------------------------------------
-- helpers

local function send(msg)
	SendAddonMessage(PREFIX, msg, "WHISPER", UnitName("player"))
end

local function has(flags, flag)
	return band(flags or 0, flag) ~= 0
end

local GOLD = "|TInterface\\MoneyFrame\\UI-GoldIcon:12:12:2:0|t"
local SILVER = "|TInterface\\MoneyFrame\\UI-SilverIcon:12:12:2:0|t"
local COPPER = "|TInterface\\MoneyFrame\\UI-CopperIcon:12:12:2:0|t"

local function thousands(n)
	local s = tostring(n)
	local sep
	repeat
		s, sep = s:gsub("^(%d+)(%d%d%d)", "%1,%2")
	until sep == 0
	return s
end

local function money(copper)
	copper = floor(tonumber(copper) or 0)
	local g, s, c = floor(copper / 10000), floor(copper / 100) % 100, copper % 100
	local parts = {}
	if g > 0 then table.insert(parts, thousands(g) .. GOLD) end
	if s > 0 then table.insert(parts, s .. SILVER) end
	if c > 0 or #parts == 0 then table.insert(parts, c .. COPPER) end
	return table.concat(parts, " ")
end

local function goldUp(copper)
	return floor(((tonumber(copper) or 0) + 9999) / 10000)
end

-- Asks the client to fetch an item it hasn't seen; GetItemInfo answers a little later.
local scanTip
local function queryItem(item)
	if GetItemInfo(item) then return end
	if not scanTip then
		scanTip = CreateFrame("GameTooltip", "BlackMarketScanTooltip", UIParent, "GameTooltipTemplate")
	end
	scanTip:SetOwner(UIParent, "ANCHOR_NONE")
	scanTip:SetHyperlink("item:" .. item .. ":0:0:0:0:0:0:0")
	scanTip:Hide()
end

local function itemInfo(item)
	local name, link, quality, _, reqLevel, _, subType, _, _, icon = GetItemInfo(item)
	if not name then
		queryItem(item)
		return "|cff808080Retrieving item information|r", nil, 1, 0, "", "Interface\\Icons\\INV_Misc_QuestionMark"
	end
	local r, g, b = GetItemQualityColor(quality or 1)
	return string.format("|cff%02x%02x%02x%s|r", r * 255, g * 255, b * 255, name), link, quality, reqLevel or 0, subType or "", icon
end

local function lockReason(l)
	if has(l.flags, LEVEL_LOCKED) then return "Come back when you are level " .. minLevel .. "." end
	if has(l.flags, ERA_LOCKED) then return "This is not for the likes of you. Not yet." end
	-- The server only sets KNOWN when it refuses those bids (BlackMarket.RefuseKnown).
	if has(l.flags, KNOWN) then return "You already have one of those." end
end

-----------------------------------------
-- DragonUI, when installed, only reskins the stock frames.

-- DragonUI's nine-slice helper: DragonUI_NineSlice since DragonUI's 2026 rewrite, NineSliceUtils
-- before it.
local NineSliceUtils = _G.DragonUI_NineSlice or _G.NineSliceUtils

local function dragon()
	local D = _G.DragonUI
	if D and D._dir and D.SafeSetAtlas and D.SkinRedButton and NineSliceUtils then
		return D
	end
end

local function skinButton(btn)
	local D = dragon()
	if D then pcall(D.SkinRedButton, btn) end
end

local function inset(parent)
	local pane = CreateFrame("Frame", nil, parent)
	pane:SetBackdrop({
		bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
		tile = true, tileSize = 16, edgeSize = 14,
		insets = { left = 3, right = 3, top = 3, bottom = 3 },
	})
	pane:SetBackdropColor(0, 0, 0, 0.75)
	pane:SetBackdropBorderColor(0.6, 0.6, 0.6, 1)
	return pane
end

-----------------------------------------
-- the window

frame = CreateFrame("Frame", "BlackMarketFrame", UIParent)
frame:SetSize(WIDTH, HEIGHT)
frame:SetPoint("CENTER", UIParent, "CENTER", 0, 40)
frame:EnableMouse(true)
frame:SetToplevel(true)
frame:SetMovable(true)
frame:SetClampedToScreen(true)
frame:SetFrameStrata("HIGH")
frame:Hide()
table.insert(UISpecialFrames, "BlackMarketFrame")

frame:SetBackdrop({
	bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
	tile = true, tileSize = 32, edgeSize = 32,
	insets = { left = 11, right = 12, top = 12, bottom = 11 },
})

local header = frame:CreateTexture(nil, "ARTWORK")
header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
header:SetSize(360, 64)
header:SetPoint("TOP", frame, "TOP", 0, 12)

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", header, "TOP", 0, -14)
title:SetText("Black Market")

-- DragonUI's metal frame, when it's dressed in one; the close button goes on top of it.
local dragonChrome

do
	local D = dragon()
	if D then
		local chrome = CreateFrame("Frame", nil, frame)
		chrome:SetAllPoints(frame)
		chrome:SetFrameLevel(frame:GetFrameLevel() + 30)
		chrome:EnableMouse(false)
		local layout = NineSliceUtils.GetLayout and (NineSliceUtils.GetLayout("NoPortraitFrameTemplate")
			or NineSliceUtils.GetLayout("PortraitFrameTemplate"))
		if layout and pcall(NineSliceUtils.ApplyLayout, chrome, layout) then
			frame:SetBackdrop(nil)
			local bg = frame:CreateTexture(nil, "BACKGROUND")
			bg:SetPoint("TOPLEFT", frame, "TOPLEFT", 2, -21)
			bg:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -2, 2)
			bg:SetTexture(0.05, 0.04, 0.04, 0.95)
			header:Hide()
			title:SetParent(chrome)
			title:ClearAllPoints()
			title:SetPoint("TOP", frame, "TOP", 0, -5)
			dragonChrome = chrome
		end
	end
end

local drag = CreateFrame("Frame", nil, frame)
drag:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, 12)
drag:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -30, 0)
drag:SetHeight(36)
drag:EnableMouse(true)
drag:RegisterForDrag("LeftButton")
drag:SetScript("OnDragStart", function () frame:StartMoving() end)
drag:SetScript("OnDragStop", function ()
	frame:StopMovingOrSizing()
	local point, _, relativePoint, x, y = frame:GetPoint(1)
	BlackMarketDB = BlackMarketDB or {}
	BlackMarketDB.position = { point, relativePoint, x, y }
end)

local close = CreateFrame("Button", "BlackMarketFrameCloseButton", frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -4)
do
	local CP = dragonChrome and _G.DragonUI.CharacterPanel
	if CP and CP.ModernizeCloseButton then
		close:ClearAllPoints()
		close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", 2, 2)
		CP.ModernizeCloseButton(close, dragonChrome, 1, 0)
		close:SetFrameLevel(dragonChrome:GetFrameLevel() + 5)
	end
end

-- Madam Goya's motto, in the corner where retail had her portrait.
local motto = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
motto:SetPoint("TOPLEFT", frame, "TOPLEFT", 22, -22)
motto:SetText("No buyouts. No refunds. No questions.")

-----------------------------------------
-- the list (left)

local LIST_WIDTH = 590
local list = inset(frame)
list:SetPoint("TOPLEFT", frame, "TOPLEFT", 18, -56)
list:SetSize(LIST_WIDTH, ROWS * ROW_HEIGHT + 10)

local COLUMNS = {
	{ "Name", 200 },
	{ "Level", 36 },
	{ "Type", 70 },
	{ "Time Left", 66 },
	{ "Seller", 96 },
	{ "Current Bid", 96 },
}

do
	local x = 6
	for _, col in ipairs(COLUMNS) do
		local label = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
		label:SetPoint("BOTTOMLEFT", list, "TOPLEFT", x, 3)
		label:SetWidth(col[2])
		label:SetJustifyH(col[1] == "Current Bid" and "RIGHT" or "LEFT")
		label:SetText(col[1])
		x = x + col[2]
	end
end

local scroll = CreateFrame("ScrollFrame", "BlackMarketListScroll", list, "FauxScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -5)
scroll:SetPoint("BOTTOMRIGHT", list, "BOTTOMRIGHT", -26, 5)

local empty = list:CreateFontString(nil, "OVERLAY", "GameFontDisable")
empty:SetPoint("CENTER", list, "CENTER", 0, 0)
empty:SetText("Nothing today. Come back after the next shipment.")

local rows = {}
local update  -- forward

local function showItemTooltip(owner, l)
	GameTooltip:SetOwner(owner, "ANCHOR_RIGHT")
	GameTooltip:SetHyperlink("item:" .. l.item .. ":0:0:0:0:0:0:0")
	GameTooltip:AddLine(" ")
	GameTooltip:AddDoubleLine("Seller", l.seller, 1, 0.82, 0, 1, 1, 1)
	if l.numBids > 0 then
		GameTooltip:AddDoubleLine("Current bid", money(l.currentBid), 1, 0.82, 0, 1, 1, 1)
		GameTooltip:AddDoubleLine("Bids", l.numBids, 1, 0.82, 0, 1, 1, 1)
	else
		GameTooltip:AddDoubleLine("Opening bid", money(l.nextBid), 1, 0.82, 0, 1, 1, 1)
	end
	if has(l.flags, HIGH_BIDDER) then
		GameTooltip:AddLine("You hold the highest bid.", 0.2, 1, 0.2)
	end
	local reason = lockReason(l)
	if reason then GameTooltip:AddLine(reason, 1, 0.25, 0.25, true) end
	GameTooltip:Show()
end

for i = 1, ROWS do
	local row = CreateFrame("Button", nil, list)
	row:SetHeight(ROW_HEIGHT)
	row:SetPoint("TOPLEFT", list, "TOPLEFT", 5, -5 - (i - 1) * ROW_HEIGHT)
	row:SetPoint("RIGHT", scroll, "RIGHT", 0, 0)
	row:RegisterForClicks("LeftButtonUp")

	local hl = row:CreateTexture(nil, "HIGHLIGHT")
	hl:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
	hl:SetBlendMode("ADD")
	hl:SetAllPoints(row)

	row.sel = row:CreateTexture(nil, "BACKGROUND")
	row.sel:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
	row.sel:SetVertexColor(1, 0.82, 0, 0.6)
	row.sel:SetBlendMode("ADD")
	row.sel:SetAllPoints(row)
	row.sel:Hide()

	row.icon = row:CreateTexture(nil, "ARTWORK")
	row.icon:SetSize(ROW_HEIGHT - 6, ROW_HEIGHT - 6)
	row.icon:SetPoint("LEFT", row, "LEFT", 2, 0)

	row.count = row:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
	row.count:SetPoint("BOTTOMRIGHT", row.icon, "BOTTOMRIGHT", -1, 1)

	row.hot = row:CreateTexture(nil, "OVERLAY")
	row.hot:SetTexture("Interface\\Icons\\Spell_Fire_Fire")
	row.hot:SetSize(12, 12)
	row.hot:SetPoint("TOPLEFT", row.icon, "TOPLEFT", -2, 2)

	local x = 6
	row.cells = {}
	for c, col in ipairs(COLUMNS) do
		local fs = row:CreateFontString(nil, "OVERLAY", c == 1 and "GameFontHighlight" or "GameFontHighlightSmall")
		if c == 1 then
			fs:SetPoint("LEFT", row.icon, "RIGHT", 6, 0)
			fs:SetWidth(col[2] - ROW_HEIGHT)
		else
			fs:SetPoint("LEFT", row, "LEFT", x, 0)
			fs:SetWidth(col[2] - 4)
		end
		fs:SetJustifyH(c == #COLUMNS and "RIGHT" or "LEFT")
		fs:SetHeight(ROW_HEIGHT)
		row.cells[c] = fs
		x = x + col[2]
	end

	row:SetScript("OnClick", function (self)
		if not self.listing then return end
		if IsModifiedClick("CHATLINK") then
			local _, link = GetItemInfo(self.listing.item)
			if link then ChatEdit_InsertLink(link) end
			return
		end
		if IsModifiedClick("DRESSUP") then
			local _, link = GetItemInfo(self.listing.item)
			if link then DressUpItemLink(link) end
			return
		end
		selected = self.listing.id
		update()
	end)
	row:SetScript("OnEnter", function (self) if self.listing then showItemTooltip(self, self.listing) end end)
	row:SetScript("OnLeave", function () GameTooltip:Hide() end)

	rows[i] = row
end

-----------------------------------------
-- the hot item (right)

local hot = inset(frame)
hot:SetPoint("TOPLEFT", list, "TOPRIGHT", 10, 0)
hot:SetPoint("BOTTOMRIGHT", list, "BOTTOMRIGHT", 10 + (WIDTH - 36 - LIST_WIDTH - 10), 0)

local hotTitle = hot:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
hotTitle:SetPoint("TOP", hot, "TOP", 0, -12)
hotTitle:SetText("Hot Item")

local hotButton = CreateFrame("Button", nil, hot)
hotButton:SetSize(64, 64)
hotButton:SetPoint("TOP", hotTitle, "BOTTOM", 0, -14)
hotButton.icon = hotButton:CreateTexture(nil, "ARTWORK")
hotButton.icon:SetAllPoints(hotButton)
local hotGlow = hotButton:CreateTexture(nil, "OVERLAY")
hotGlow:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
hotGlow:SetBlendMode("ADD")
hotGlow:SetVertexColor(1, 0.3, 0.1)
hotGlow:SetPoint("CENTER", hotButton, "CENTER", 0, 0)
hotGlow:SetSize(118, 118)

local hotName = hot:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
hotName:SetPoint("TOP", hotButton, "BOTTOM", 0, -10)
hotName:SetWidth(180)

local hotSeller = hot:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hotSeller:SetPoint("TOP", hotName, "BOTTOM", 0, -8)
hotSeller:SetWidth(180)

local hotBid = hot:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
hotBid:SetPoint("TOP", hotSeller, "BOTTOM", 0, -14)
hotBid:SetWidth(180)

local hotTime = hot:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hotTime:SetPoint("TOP", hotBid, "BOTTOM", 0, -6)

local hotNone = hot:CreateFontString(nil, "OVERLAY", "GameFontDisable")
hotNone:SetPoint("CENTER", hot, "CENTER", 0, 0)
hotNone:SetWidth(190)
hotNone:SetText("Nothing hot today.")

local hotSelect = CreateFrame("Button", "BlackMarketHotSelect", hot, "UIPanelButtonTemplate")
hotSelect:SetSize(120, 22)
hotSelect:SetPoint("BOTTOM", hot, "BOTTOM", 0, 14)
hotSelect:SetText("Select")
skinButton(hotSelect)

-----------------------------------------
-- the bid bar (bottom)

local bar = CreateFrame("Frame", nil, frame)
bar:SetPoint("TOPLEFT", list, "BOTTOMLEFT", 0, -10)
bar:SetPoint("RIGHT", hot, "RIGHT", 0, 0)
bar:SetHeight(64)

local selName = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
selName:SetPoint("TOPLEFT", bar, "TOPLEFT", 4, -4)
selName:SetWidth(420)
selName:SetJustifyH("LEFT")

local selInfo = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
selInfo:SetPoint("TOPLEFT", selName, "BOTTOMLEFT", 0, -4)
selInfo:SetWidth(420)
selInfo:SetJustifyH("LEFT")

local bidButton = CreateFrame("Button", "BlackMarketBidButton", bar, "UIPanelButtonTemplate")
bidButton:SetSize(100, 22)
bidButton:SetPoint("TOPRIGHT", bar, "TOPRIGHT", 0, -2)
bidButton:SetText(BID or "Bid")
skinButton(bidButton)

local goldIcon = bar:CreateTexture(nil, "ARTWORK")
goldIcon:SetTexture("Interface\\MoneyFrame\\UI-GoldIcon")
goldIcon:SetSize(13, 13)
goldIcon:SetPoint("RIGHT", bidButton, "LEFT", -10, 0)

local bidBox = CreateFrame("EditBox", "BlackMarketBidBox", bar, "InputBoxTemplate")
bidBox:SetSize(100, 20)
bidBox:SetPoint("RIGHT", goldIcon, "LEFT", -4, 0)
bidBox:SetAutoFocus(false)
bidBox:SetNumeric(true)
bidBox:SetMaxLetters(6)
bidBox:SetJustifyH("RIGHT")

local bidLabel = bar:CreateFontString(nil, "OVERLAY", "GameFontNormal")
bidLabel:SetPoint("RIGHT", bidBox, "LEFT", -10, 0)
bidLabel:SetText("Your bid")

local yourMoney = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
yourMoney:SetPoint("TOPRIGHT", bidButton, "BOTTOMRIGHT", 0, -10)

local status = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
status:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 22, 18)
status:SetWidth(560)
status:SetJustifyH("LEFT")

local statusToken = 0
local timerFrame = CreateFrame("Frame")
local timers = {}
timerFrame:Hide()
timerFrame:SetScript("OnUpdate", function (self)
	local now = GetTime()
	for i = #timers, 1, -1 do
		if timers[i].at <= now then
			local t = table.remove(timers, i)
			t.fn()
		end
	end
	if #timers == 0 then self:Hide() end
end)

local function after(delay, fn)
	table.insert(timers, { at = GetTime() + delay, fn = fn })
	timerFrame:Show()
end

local function setStatus(text, isError)
	statusToken = statusToken + 1
	local mine = statusToken
	status:SetText(text or "")
	if isError then status:SetTextColor(1, 0.25, 0.25) else status:SetTextColor(1, 0.82, 0) end
	after(8, function () if mine == statusToken then status:SetText("") end end)
end

-----------------------------------------
-- drawing

local function findListing(id)
	for _, l in ipairs(listings) do
		if l.id == id then return l end
	end
end

local missingInfo = false
local redrawPending = false

local function drawRow(row, l)
	row.listing = l
	if not l then
		row:Hide()
		return
	end
	row:Show()

	local name, _, _, reqLevel, subType, icon = itemInfo(l.item)
	if not GetItemInfo(l.item) then missingInfo = true end
	row.icon:SetTexture(icon)
	row.count:SetText(l.quantity > 1 and l.quantity or "")
	if has(l.flags, HOT) then row.hot:Show() else row.hot:Hide() end

	local locked = lockReason(l) ~= nil
	row.icon:SetDesaturated(locked)

	row.cells[1]:SetText(name)
	row.cells[2]:SetText(reqLevel > 0 and reqLevel or "")
	row.cells[3]:SetText(subType)
	local tl = TIME_LEFT[l.timeLeft]
	if tl then
		row.cells[4]:SetText(tl[1])
		row.cells[4]:SetTextColor(tl[2], tl[3], tl[4])
	else
		row.cells[4]:SetText("")
	end
	row.cells[5]:SetText(l.seller)
	local bidText = money(l.numBids > 0 and l.currentBid or l.nextBid)
	if has(l.flags, HIGH_BIDDER) then
		bidText = "|cff20ff20" .. "(you) |r" .. bidText
	elseif l.numBids == 0 then
		bidText = "|cff808080" .. bidText .. "|r"
	end
	row.cells[6]:SetText(bidText)
	for c = 2, 6 do
		if c ~= 4 then
			if locked then row.cells[c]:SetTextColor(0.6, 0.6, 0.6) else row.cells[c]:SetTextColor(1, 1, 1) end
		end
	end

	if selected == l.id then row.sel:Show() else row.sel:Hide() end
end

local function drawHot()
	local h
	for _, l in ipairs(listings) do
		if has(l.flags, HOT) then h = l break end
	end
	hot.listing = h
	local parts = { hotTitle, hotButton, hotName, hotSeller, hotBid, hotTime, hotSelect }
	if not h then
		for _, p in ipairs(parts) do p:Hide() end
		hotTitle:Show()
		hotNone:Show()
		return
	end
	for _, p in ipairs(parts) do p:Show() end
	hotNone:Hide()

	local name, _, _, _, _, icon = itemInfo(h.item)
	hotButton.icon:SetTexture(icon)
	hotName:SetText(name)
	hotSeller:SetText("Seller: " .. h.seller)
	if h.numBids > 0 then
		hotBid:SetText(money(h.currentBid) .. "  (" .. h.numBids .. (h.numBids == 1 and " bid)" or " bids)"))
	else
		hotBid:SetText("Opening bid " .. money(h.nextBid))
	end
	local tl = TIME_LEFT[h.timeLeft]
	hotTime:SetText(tl and ("Time left: " .. tl[1]) or "")
end

local function drawSelection()
	local l = selected and findListing(selected)
	if not l then
		selected = nil
		selName:SetText("Pick something from the list.")
		selInfo:SetText("")
		bidBox:SetText("")
		bidButton:Disable()
		return
	end

	local name = itemInfo(l.item)
	selName:SetText(name)

	local reason = lockReason(l)
	if has(l.flags, HIGH_BIDDER) then
		selInfo:SetText("|cff20ff20You hold the highest bid.|r")
		bidButton:Disable()
	elseif reason then
		selInfo:SetText("|cffff4040" .. reason .. "|r")
		bidButton:Disable()
	else
		local line = l.numBids > 0
			and string.format("Current bid %s (%d %s). Minimum bid %s.", money(l.currentBid), l.numBids, l.numBids == 1 and "bid" or "bids", money(l.nextBid))
			or ("Opening bid " .. money(l.nextBid) .. ".")
		selInfo:SetText(line)
		bidButton:Enable()
	end

	-- Only fill in the minimum for a new pick (or an empty box): a redraw after somebody else's
	-- bid must not throw away the amount the player typed. Too low an amount is caught on Bid.
	if bidBox.listing ~= l.id or bidBox:GetText() == "" then
		bidBox:SetText(goldUp(l.nextBid))
		bidBox.listing = l.id
	end
end

update = function ()
	if not frame:IsShown() then return end
	missingInfo = false
	local offset = FauxScrollFrame_GetOffset(scroll)
	for i = 1, ROWS do
		drawRow(rows[i], listings[i + offset])
	end
	FauxScrollFrame_Update(scroll, #listings, ROWS, ROW_HEIGHT)
	if #listings == 0 then empty:Show() else empty:Hide() end
	drawHot()
	drawSelection()
	yourMoney:SetText(money(GetMoney()))

	-- Items the client hadn't cached yet: draw again once they've arrived.
	if missingInfo and not redrawPending then
		redrawPending = true
		after(1, function () redrawPending = false update() end)
	end
end

scroll:SetScript("OnVerticalScroll", function (self, offset)
	FauxScrollFrame_OnVerticalScroll(self, offset, ROW_HEIGHT, update)
end)

hotSelect:SetScript("OnClick", function ()
	if hot.listing then
		selected = hot.listing.id
		update()
	end
end)
hotButton:SetScript("OnEnter", function (self) if hot.listing then showItemTooltip(self, hot.listing) end end)
hotButton:SetScript("OnLeave", function () GameTooltip:Hide() end)
hotButton:SetScript("OnClick", function ()
	if hot.listing then
		selected = hot.listing.id
		update()
	end
end)

local function placeBid()
	local l = selected and findListing(selected)
	if not l then return end
	local gold = tonumber(bidBox:GetText())
	if not gold or gold <= 0 then
		setStatus("Numbers, friend. Just the gold.", true)
		return
	end
	local copper = gold * 10000
	if copper < l.nextBid then
		setStatus("The least I take is " .. money(goldUp(l.nextBid) * 10000) .. ".", true)
		return
	end
	if copper > GetMoney() then
		setStatus("You don't have enough money.", true)
		return
	end
	bidBox:ClearFocus()
	local itemName = itemInfo(l.item)
	local dialog = StaticPopup_Show("BLACKMARKET_CONFIRM_BID", money(copper), itemName)
	if dialog then
		dialog.data = { id = l.id, copper = copper }
	end
end

StaticPopupDialogs["BLACKMARKET_CONFIRM_BID"] = {
	text = "Bid %s on %s?\n\nA bid can't be taken back.",
	button1 = BID or "Bid",
	button2 = CANCEL,
	OnAccept = function (self, data)
		data = data or self.data
		if data then
			send("BID:" .. data.id .. ":" .. data.copper)
			setStatus("Placing your bid...")
		end
	end,
	timeout = 0,
	whileDead = 1,
	hideOnEscape = 1,
}

bidButton:SetScript("OnClick", placeBid)
bidBox:SetScript("OnEnterPressed", placeBid)
bidBox:SetScript("OnEscapePressed", function (self) self:ClearFocus() end)

frame:SetScript("OnShow", function ()
	PlaySound("AuctionWindowOpen")
	frame:ClearAllPoints()
	local p = BlackMarketDB and BlackMarketDB.position
	if p then
		frame:SetPoint(p[1], UIParent, p[2], p[3], p[4])
	else
		frame:SetPoint("CENTER", UIParent, "CENTER", 0, 40)
	end
	status:SetText("")
	update()
end)

frame:SetScript("OnHide", function ()
	PlaySound("AuctionWindowClose")
	StaticPopup_Hide("BLACKMARKET_CONFIRM_BID")
end)

-----------------------------------------
-- server messages

local function parseRow(text)
	local id, item, quantity, nextBid, currentBid, numBids, timeLeft, flags, ipState, seller =
		strsplit(",", text, 10)
	id = tonumber(id)
	if not id then return end
	return {
		id = id,
		item = tonumber(item) or 0,
		quantity = tonumber(quantity) or 1,
		nextBid = tonumber(nextBid) or 0,
		currentBid = tonumber(currentBid) or 0,
		numBids = tonumber(numBids) or 0,
		timeLeft = tonumber(timeLeft) or 0,
		flags = tonumber(flags) or 0,
		ipState = tonumber(ipState) or 0,
		seller = seller or "",
	}
end

local function onMessage(msg)
	local cmd, rest = msg:match("^([^:]+):?(.*)$")
	if cmd == "OPEN" then
		-- The server sends the gossip too, for players without the addon: put it away.
		CloseGossip()
		frame:Show()
	elseif cmd == "LB" then
		incoming = {}
	elseif cmd == "L" then
		incoming = incoming or {}
		for part in rest:gmatch("[^;]+") do
			local l = parseRow(part)
			if l then
				table.insert(incoming, l)
				queryItem(l.item)
			end
		end
	elseif cmd == "LE" then
		minLevel = tonumber(rest) or minLevel
		listings = incoming or {}
		incoming = nil
		if selected and not findListing(selected) then selected = nil end
		if not selected then
			for _, l in ipairs(listings) do
				if has(l.flags, HOT) then selected = l.id break end
			end
			selected = selected or (listings[1] and listings[1].id)
		end
		update()
	elseif cmd == "CHG" then
		if frame:IsShown() then send("LIST") end
	elseif cmd == "BID" then
		local result, id, text = rest:match("^(%a+):(%d+):?(.*)$")
		if result == "OK" then
			PlaySound("LOOTWINDOWCOINSOUND")
			setStatus("Your bid is in. Keep it quiet.")
		else
			setStatus(text ~= "" and text or "Your bid was refused.", true)
		end
		if id then selected = tonumber(id) end
	elseif cmd == "OUTBID" then
		local l = findListing(tonumber(rest))
		local name = l and (GetItemInfo(l.item)) or "an item"
		UIErrorsFrame:AddMessage("Black Market: you have been outbid on " .. name .. ".", 1, 0.25, 0.25)
		if frame:IsShown() then send("LIST") end
	end
end

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("PLAYER_MONEY")
events:SetScript("OnEvent", function (_, event, prefix, msg, channel, sender)
	if event == "PLAYER_ENTERING_WORLD" then
		-- Tells the server this character has the window, so the broker opens it instead of gossip.
		send("HELLO:" .. PROTOCOL)
	elseif event == "PLAYER_MONEY" then
		if frame:IsShown() then yourMoney:SetText(money(GetMoney())) end
	elseif event == "CHAT_MSG_ADDON" then
		if prefix == PREFIX and channel == "WHISPER" and sender == UnitName("player") then
			onMessage(msg)
		end
	end
end)

SLASH_BLACKMARKET1 = "/blackmarket"
SLASH_BLACKMARKET2 = "/bmah"
SlashCmdList.BLACKMARKET = function (arg)
	if arg == "reset" then
		BlackMarketDB = BlackMarketDB or {}
		BlackMarketDB.position = nil
		if frame:IsShown() then frame:Hide() frame:Show() end
		DEFAULT_CHAT_FRAME:AddMessage("Black Market: window position reset.")
	else
		DEFAULT_CHAT_FRAME:AddMessage("Black Market: talk to Mister Chu to open the market. /bmah reset puts the window back in the middle.")
	end
end

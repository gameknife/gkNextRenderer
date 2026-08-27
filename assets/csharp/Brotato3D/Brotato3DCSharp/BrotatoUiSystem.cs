using GkNext;
using GkNext.Interop;

namespace Brotato3D;

public sealed partial class Brotato3DCSharpGameInstance
{
    protected override bool OnRenderUI()
    {
        if (!configurationReady)
        {
            return false;
        }
        gui.BeginFrame();
        switch (state)
        {
        case AppState.MainMenu:
            DrawMainMenu();
            break;
        case AppState.CharacterSelect:
            DrawCharacterSelect();
            break;
        case AppState.Playing:
            DrawHud();
            break;
        case AppState.Paused:
            DrawHud();
            DrawPauseMenu();
            break;
        case AppState.LevelUpPicking:
            DrawHud();
            DrawUpgradePicker();
            break;
        case AppState.Shopping:
            DrawHud();
            DrawShop();
            break;
        case AppState.Result:
            DrawResult();
            break;
        }
        gui.EndFrame();
        return false;
    }

    private void DrawMainMenu()
    {
        Vector2 screen = gui.ScreenSize;
        DrawDim(Color.FromBytes(5, 10, 14, 128));
        float width = Math.Min(560.0f, screen.X - 40.0f);
        UiRect panel = new((screen.X - width) * 0.5f, Math.Max(40.0f, screen.Y * 0.5f - 220.0f), width, 420.0f);
        gui.Panel(panel, 20.0f);
        DrawPotatoEmblem(new Vector2(screen.X * 0.5f, panel.Y + 82.0f), 46.0f);
        gui.DrawTextCenteredX("BROTATO 3D", panel.Y + 142.0f, Color.FromBytes(255, 219, 92), 2.15f);
        gui.DrawTextCenteredX("C# gameplay implementation", panel.Y + 190.0f,
                              Color.FromBytes(176, 207, 218), 1.0f);
        UiRect start = new(panel.X + 82.0f, panel.Y + 244.0f, panel.Width - 164.0f, 54.0f);
        if (gui.Button(100, start, "START RUN", sceneReady, 1.25f))
        {
            SetAppState(AppState.CharacterSelect);
            PlaySfx("ui_click.wav", 0.55f, 35);
        }
        UiRect exit = new(start.X, start.Y + 70.0f, start.Width, 48.0f);
        if (gui.Button(101, exit, "EXIT", true, 1.05f))
        {
            Engine.RequestClose();
        }
    }

    private void DrawCharacterSelect()
    {
        Vector2 screen = gui.ScreenSize;
        DrawDim(Color.FromBytes(5, 9, 13, 155));
        float width = Math.Min(1040.0f, screen.X - 36.0f);
        float height = Math.Min(650.0f, screen.Y - 36.0f);
        UiRect panel = new((screen.X - width) * 0.5f, (screen.Y - height) * 0.5f, width, height);
        gui.Panel(panel, 18.0f);
        gui.DrawTextCenteredX("CHOOSE YOUR BROTATO", panel.Y + 30.0f, Color.FromBytes(255, 224, 105), 1.65f);

        float arenaY = panel.Y + 91.0f;
        UiRect previousArena = new(panel.X + 32.0f, arenaY, 58.0f, 38.0f);
        UiRect nextArena = new(panel.Right - 90.0f, arenaY, 58.0f, 38.0f);
        if (gui.Button(200, previousArena, "<", sceneReady))
        {
            ChangeArena(-1);
        }
        if (gui.Button(201, nextArena, ">", sceneReady))
        {
            ChangeArena(1);
        }
        gui.DrawTextCentered(CurrentArena.Id,
                             new UiRect(previousArena.Right + 16.0f, arenaY,
                                        nextArena.X - previousArena.Right - 32.0f, 38.0f),
                             Color.FromBytes(196, 224, 231), 1.05f);

        int characterCount = database.Characters.Count;
        float gap = 18.0f;
        float cardsWidth = panel.Width - 64.0f;
        float cardWidth = (cardsWidth - gap * Math.Max(0, characterCount - 1)) / Math.Max(1, characterCount);
        float cardY = panel.Y + 160.0f;
        for (int index = 0; index < characterCount; ++index)
        {
            CharacterDef character = database.Characters[index];
            bool keyboardFocused = index == selectedCharacterIndex;
            UiRect card = new(panel.X + 32.0f + index * (cardWidth + gap), cardY, cardWidth, 330.0f);
            gui.DrawList.AddRectFilled(card, Color.FromBytes(23, 32, 42, 245), 14.0f);
            gui.DrawList.AddRect(card,
                                 keyboardFocused ? Color.FromBytes(255, 224, 105) : CharacterColor(character, 200),
                                 14.0f, keyboardFocused ? 4.0f : 2.0f);
            Vector2 portrait = new(card.X + card.Width * 0.5f, card.Y + 82.0f);
            gui.DrawList.AddCircleFilled(portrait, 43.0f, CharacterColor(character, 255), 48);
            gui.DrawList.AddCircle(portrait, 43.0f, Color.FromBytes(255, 255, 255, 180), 2.0f, 48);
            gui.DrawTextCentered(character.Name, new UiRect(card.X, card.Y + 145.0f, card.Width, 38.0f),
                                 Color.White, 1.25f);
            gui.DrawTextCentered(character.StartWeapon.ToUpperInvariant(),
                                 new UiRect(card.X, card.Y + 190.0f, card.Width, 28.0f),
                                 Color.FromBytes(255, 211, 93), 0.95f);
            gui.DrawTextCentered($"HP {(int)character.StartStats.MaxHpFlat}",
                                 new UiRect(card.X, card.Y + 224.0f, card.Width, 24.0f),
                                 Color.FromBytes(208, 225, 232), 0.85f);
            UiRect choose = new(card.X + 24.0f, card.Bottom - 62.0f, card.Width - 48.0f, 42.0f);
            if (gui.Button(220 + index, choose, keyboardFocused ? "SELECT [ENTER]" : "SELECT", sceneReady, 1.0f))
            {
                SelectCharacter(index);
            }
        }

        UiRect back = new(panel.X + 32.0f, panel.Bottom - 58.0f, 130.0f, 36.0f);
        if (gui.Button(299, back, "BACK"))
        {
            SetAppState(AppState.MainMenu);
        }
    }

    private void DrawHud()
    {
        Vector2 screen = gui.ScreenSize;
        float barWidth = Math.Min(340.0f, screen.X * 0.28f);
        gui.ProgressBar(new UiRect(24.0f, screen.Y - 78.0f, barWidth, 24.0f),
                        player.MaxHp > 0 ? player.CurrentHp / (float)player.MaxHp : 0.0f,
                        Color.FromBytes(214, 62, 58),
                        $"HP {player.CurrentHp} / {player.MaxHp}");
        int xpTarget = XpToNextLevel();
        gui.ProgressBar(new UiRect(24.0f, screen.Y - 45.0f, barWidth, 18.0f),
                        xpTarget > 0 ? player.CurrentXp / (float)xpTarget : 0.0f,
                        Color.FromBytes(66, 133, 242),
                        $"LV {player.Level}   XP {player.CurrentXp}/{xpTarget}");

        gui.DrawTextCenteredX($"WAVE {Math.Min(currentWaveIndex + 1, database.Waves.Count)} / {database.Waves.Count}",
                              15.0f, Color.White, 1.18f);
        gui.DrawTextCenteredX(FormatTime(waveRemainingSeconds), 42.0f,
                              waveRemainingSeconds < 5.0f ? Color.FromBytes(255, 80, 62) : Color.FromBytes(255, 222, 112),
                              1.45f);
        gui.DrawList.AddText($"MATERIALS {player.Materials}", new Vector2(screen.X - 210.0f, 24.0f),
                             Color.FromBytes(255, 218, 72), 1.05f);
        gui.DrawList.AddText($"KILLS {killCount}", new Vector2(screen.X - 210.0f, 51.0f), Color.White, 0.95f);

        float chargeX = 24.0f;
        float chargeY = screen.Y - 111.0f;
        for (int charge = 0; charge < 3; ++charge)
        {
            Color color = charge < player.DashCharges ? Color.FromBytes(83, 221, 244) : Color.FromBytes(39, 47, 54, 220);
            gui.DrawList.AddRectFilled(new UiRect(chargeX + charge * 22.0f, chargeY, 15.0f, 15.0f), color, 3.0f);
        }

        if (state == AppState.Playing)
        {
            UiRect pause = new(screen.X - 72.0f, screen.Y - 62.0f, 46.0f, 38.0f);
            if (gui.Button(300, pause, "II"))
            {
                SetAppState(AppState.Paused);
            }
        }
    }

    private void DrawPauseMenu()
    {
        UiRect panel = CenterPanel(430.0f, 320.0f);
        DrawDim(Color.FromBytes(0, 0, 0, 155));
        gui.Panel(panel, 18.0f);
        gui.DrawTextCentered("PAUSED", new UiRect(panel.X, panel.Y + 32.0f, panel.Width, 52.0f),
                             Color.FromBytes(255, 224, 105), 1.8f);
        UiRect resume = new(panel.X + 72.0f, panel.Y + 122.0f, panel.Width - 144.0f, 50.0f);
        if (gui.Button(400, resume, "RESUME", true, 1.15f))
        {
            SetAppState(AppState.Playing);
        }
        UiRect menu = new(resume.X, resume.Y + 70.0f, resume.Width, 48.0f);
        if (gui.Button(401, menu, "MAIN MENU"))
        {
            GoToMainMenu();
        }
    }

    private void DrawUpgradePicker()
    {
        DrawDim(Color.FromBytes(0, 0, 0, 175));
        UiRect panel = CenterPanel(Math.Min(920.0f, gui.ScreenSize.X - 30.0f), 430.0f);
        gui.Panel(panel, 18.0f);
        gui.DrawTextCentered("LEVEL UP — PICK ONE", new UiRect(panel.X, panel.Y + 28.0f, panel.Width, 48.0f),
                             Color.FromBytes(113, 222, 255), 1.5f);
        DrawChoiceCards(panel, upgradeChoices.Length, (index, card) =>
        {
            UpgradeDef? choice = upgradeChoices[index];
            string title = choice?.Name ?? "—";
            gui.DrawTextCentered(title, new UiRect(card.X + 10.0f, card.Y + 35.0f, card.Width - 20.0f, 70.0f),
                                 Color.White, 1.05f);
            gui.DrawTextCentered(choice?.Stat ?? string.Empty,
                                 new UiRect(card.X + 10.0f, card.Y + 114.0f, card.Width - 20.0f, 40.0f),
                                 Color.FromBytes(157, 204, 219), 0.85f);
            if (gui.Button(500 + index, new UiRect(card.X + 22.0f, card.Bottom - 62.0f, card.Width - 44.0f, 42.0f),
                           "TAKE", choice is not null))
            {
                SelectUpgrade(index);
            }
        });
    }

    private void DrawShop()
    {
        DrawDim(Color.FromBytes(3, 7, 10, 190));
        UiRect panel = CenterPanel(Math.Min(980.0f, gui.ScreenSize.X - 30.0f), 510.0f);
        gui.Panel(panel, 18.0f);
        gui.DrawTextCentered("WAVE CLEARED — SHOP", new UiRect(panel.X, panel.Y + 22.0f, panel.Width, 46.0f),
                             Color.FromBytes(255, 218, 80), 1.45f);
        gui.DrawTextCentered($"MATERIALS {player.Materials}", new UiRect(panel.X, panel.Y + 68.0f, panel.Width, 30.0f),
                             Color.White, 1.0f);
        DrawChoiceCards(new UiRect(panel.X, panel.Y + 65.0f, panel.Width, panel.Height - 65.0f), shopOffers.Length,
            (index, card) =>
            {
                ShopItemDef? offer = shopOffers[index];
                gui.DrawTextCentered(offer?.Name ?? "SOLD",
                                     new UiRect(card.X + 8.0f, card.Y + 30.0f, card.Width - 16.0f, 74.0f),
                                     offer is null ? Color.FromBytes(110, 118, 124) : Color.White, 1.0f);
                if (offer is not null)
                {
                    gui.DrawTextCentered($"{offer.Cost} MATERIALS",
                                         new UiRect(card.X, card.Y + 120.0f, card.Width, 32.0f),
                                         player.Materials >= offer.Cost ? Color.FromBytes(255, 219, 78) : Color.FromBytes(255, 102, 88),
                                         0.9f);
                }
                if (gui.Button(600 + index,
                               new UiRect(card.X + 22.0f, card.Bottom - 62.0f, card.Width - 44.0f, 42.0f),
                               offer is null ? "SOLD" : "BUY",
                               offer is not null && player.Materials >= offer.Cost))
                {
                    BuyShopOffer(index);
                }
            });
        UiRect next = new(panel.Right - 190.0f, panel.Bottom - 56.0f, 155.0f, 38.0f);
        if (gui.Button(699, next, "NEXT WAVE"))
        {
            ContinueFromShop();
        }
    }

    private void DrawResult()
    {
        DrawDim(Color.FromBytes(2, 6, 9, 175));
        UiRect panel = CenterPanel(520.0f, 410.0f);
        gui.Panel(panel, 20.0f);
        gui.DrawTextCentered(resultVictory ? "VICTORY" : "RUN ENDED",
                             new UiRect(panel.X, panel.Y + 36.0f, panel.Width, 58.0f),
                             resultVictory ? Color.FromBytes(255, 222, 83) : Color.FromBytes(255, 105, 91),
                             1.9f);
        gui.DrawTextCentered($"WAVE {currentWaveIndex + 1}", new UiRect(panel.X, panel.Y + 125.0f, panel.Width, 30.0f),
                             Color.White, 1.05f);
        gui.DrawTextCentered($"KILLS {killCount}", new UiRect(panel.X, panel.Y + 164.0f, panel.Width, 30.0f),
                             Color.White, 1.05f);
        gui.DrawTextCentered($"TIME {FormatTime(runElapsedSeconds)}", new UiRect(panel.X, panel.Y + 203.0f, panel.Width, 30.0f),
                             Color.White, 1.05f);
        UiRect retry = new(panel.X + 82.0f, panel.Bottom - 126.0f, panel.Width - 164.0f, 48.0f);
        if (gui.Button(700, retry, "RETRY", sceneReady, 1.1f))
        {
            StartNewRun();
        }
        UiRect menu = new(retry.X, retry.Y + 62.0f, retry.Width, 42.0f);
        if (gui.Button(701, menu, "MAIN MENU"))
        {
            GoToMainMenu();
        }
    }

    private void DrawChoiceCards(UiRect panel, int count, Action<int, UiRect> drawCard)
    {
        float gap = 18.0f;
        float margin = 32.0f;
        float width = (panel.Width - margin * 2.0f - gap * Math.Max(0, count - 1)) / Math.Max(1, count);
        float y = panel.Y + 100.0f;
        float height = Math.Max(220.0f, panel.Height - 145.0f);
        for (int index = 0; index < count; ++index)
        {
            UiRect card = new(panel.X + margin + index * (width + gap), y, width, height);
            gui.DrawList.AddRectFilled(card, Color.FromBytes(24, 34, 44, 248), 12.0f);
            gui.DrawList.AddRect(card, Color.FromBytes(112, 151, 170, 170), 12.0f, 1.5f);
            drawCard(index, card);
        }
    }

    private void DrawDim(Color color)
        => gui.DrawList.AddRectFilled(new UiRect(0.0f, 0.0f, gui.ScreenSize.X, gui.ScreenSize.Y),
                                      color, 0.0f, UiDrawLayer.Background);

    private UiRect CenterPanel(float width, float height)
        => new((gui.ScreenSize.X - width) * 0.5f, (gui.ScreenSize.Y - height) * 0.5f, width, height);

    private void DrawPotatoEmblem(Vector2 center, float radius)
    {
        gui.DrawList.AddCircleFilled(center, radius, Color.FromBytes(184, 126, 67), 48);
        gui.DrawList.AddCircle(center, radius, Color.FromBytes(255, 224, 153), 2.2f, 48);
        gui.DrawList.AddCircleFilled(new Vector2(center.X - 15.0f, center.Y - 7.0f), 4.0f, Color.FromBytes(28, 24, 22), 16);
        gui.DrawList.AddCircleFilled(new Vector2(center.X + 15.0f, center.Y - 7.0f), 4.0f, Color.FromBytes(28, 24, 22), 16);
        Span<Vector2> smile = stackalloc Vector2[3]
        {
            new(center.X - 16.0f, center.Y + 12.0f),
            new(center.X, center.Y + 21.0f),
            new(center.X + 16.0f, center.Y + 12.0f),
        };
        gui.DrawList.AddPolyline(smile, Color.FromBytes(55, 34, 25), 3.0f);
    }

    private static Color CharacterColor(CharacterDef character, byte alpha)
        => new(character.Color.X, character.Color.Y, character.Color.Z, alpha / 255.0f);

    private static string FormatTime(float seconds)
    {
        int whole = Math.Max(0, (int)MathF.Ceiling(seconds));
        return $"{whole / 60:00}:{whole % 60:00}";
    }
}

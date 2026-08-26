# C# Managed Scripting Development

gkNextEngine supports modern C# (.NET) for gameplay authoring, featuring **CoreCLR (instant hot reload during development)** and **NativeAOT (native zero-overhead for release)** backends running identical managed assemblies.

---

## 🎮 Writing a C# Game Instance

Derive from `NextGameInstance` and annotate with `[GameInstance]`:

```csharp
using GkNext.Engine;

[GameInstance]
public class MyAwesomeGame : NextGameInstance
{
    public override void OnInit()
    {
        GkLog.Info("MyAwesomeGame initialized!");
    }

    public override void OnTick(float deltaTime)
    {
        if (Input.IsKeyDown(KeyCode.Space))
        {
            GkLog.Info("Jump!");
        }
    }
}
```

---

## ⚡ Core Patterns

- **Game Manifest**: Declare window attributes, scenes, and reload policies in `assets/configs/games/<id>.game.json`.
- **Node & Component Access**:
  ```csharp
  var player = Scene.FindNode("Player");
  if (player.IsValid)
  {
      player.Translation += new Vector3(0, 0, 1.0f * deltaTime);
      player.GetComponent<RenderComponent>().Visible = true;
  }
  ```
- **Play-in-Editor (PIE)**: Press `F5` in `gkNextEditor` to run games inside the viewport; press `F8` to eject and inspect ECS properties live.

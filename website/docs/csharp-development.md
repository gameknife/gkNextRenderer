# C# 托管脚本开发指南 (C# Game Development)

gkNextEngine 支持使用现代 C# (.NET) 编写游戏逻辑，采用 **CoreCLR（开发期秒级热重载）** 与 **NativeAOT（发布期原生零开销）** 双后端架构。两套后端运行完全相同的托管 C# 代码。

---

## 🎮 快速创建 C# 游戏

编写一个 C# 游戏只需继承 `NextGameInstance` 并添加 `[GameInstance]` 特性：

```csharp
using GkNext.Engine;

[GameInstance]
public class MySuperGame : NextGameInstance
{
    public override void OnInit()
    {
        GkLog.Info("MySuperGame 启动成功！");
    }

    public override void OnTick(float deltaTime)
    {
        // 游戏每帧更新逻辑
        if (Input.IsKeyDown(KeyCode.Space))
        {
            GkLog.Info("Jump!");
        }
    }
}
```

---

## ⚡ 核心机制与开发规范

1. **统一游戏配置声明**：每个 C# 游戏在 `assets/configs/games/<id>.game.json` 声明窗口标题、初始场景、模块需求与热重载策略。
2. **场景节点与组件访问**：
   ```csharp
   // 获取当前场景节点
   var playerNode = Scene.FindNode("Player");
   if (playerNode.IsValid)
   {
       playerNode.Translation += new Vector3(0, 0, 1.0f * deltaTime);
       playerNode.GetComponent<RenderComponent>().Visible = true;
   }
   ```
3. **Play-in-Editor (PIE)**：在 `gkNextEditor` 中按 `F5` 即可在编辑器内直接运行 C# 游戏；按 `F8` 弹出查看并修改运行时属性，停止后场景自动还原。

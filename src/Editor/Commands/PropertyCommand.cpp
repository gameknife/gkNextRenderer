#include "PropertyCommand.h"
#include "Assets/Node.h"

namespace Editor
{
    bool TransformCommand::Execute()
    {
        if (!node_)
        {
            return false;
        }
        
        switch (type_)
        {
            case TransformType::Translation:
                node_->SetTranslation(newValue_);
                break;
            case TransformType::Rotation:
                if (isQuat_)
                {
                    node_->SetRotation(newRotation_);
                }
                else
                {
                    node_->SetRotation(glm::quat(newValue_));
                }
                break;
            case TransformType::Scale:
                node_->SetScale(newValue_);
                break;
        }
        
        node_->RecalcTransform(true);
        return true;
    }
    
    bool TransformCommand::Undo()
    {
        if (!node_)
        {
            return false;
        }
        
        switch (type_)
        {
            case TransformType::Translation:
                node_->SetTranslation(oldValue_);
                break;
            case TransformType::Rotation:
                if (isQuat_)
                {
                    node_->SetRotation(oldRotation_);
                }
                else
                {
                    node_->SetRotation(glm::quat(oldValue_));
                }
                break;
            case TransformType::Scale:
                node_->SetScale(oldValue_);
                break;
        }
        
        node_->RecalcTransform(true);
        return true;
    }
    
    std::string TransformCommand::GetDescription() const
    {
        const char* typeStr = "Unknown";
        switch (type_)
        {
            case TransformType::Translation:
                typeStr = "Translation";
                break;
            case TransformType::Rotation:
                typeStr = "Rotation";
                break;
            case TransformType::Scale:
                typeStr = "Scale";
                break;
        }
        
        return fmt::format("Set {} on {}", typeStr, node_ ? node_->GetName() : "null");
    }
}

#include <catch2/catch_all.hpp>
#include "TestCommon.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

// Define a concrete component for testing
class TestComponent : public Assets::Component {
public:
    TestComponent() = default;
    int value = 0;
    
    std::string_view GetTypeName() const override { return "TestComponent"; }
    entt::id_type GetTypeId() const override { return Assets::ComponentTypeId<TestComponent>(); }
    entt::meta_type GetMetaType() const override { return entt::resolve<TestComponent>(); }
};

TEST_CASE("Component System Basics", "[Unit][Component]") {
    // CreateNode(name, translation, rotation, scale, instanceId)
    auto node = Assets::Node::CreateNode("TestNode", glm::vec3(0), glm::quat(1,0,0,0), glm::vec3(1), 0);
    
    SECTION("Add and Get Component") {
        auto comp = std::make_shared<TestComponent>();
        comp->value = 42;
        
        node->AddComponent(comp);
        
        auto retrieved = node->GetComponent<TestComponent>();
        REQUIRE(retrieved != nullptr);
        CHECK(retrieved->value == 42);
        CHECK(node->GetComponent("TestComponent") == retrieved);
    }
    
    SECTION("Uniqueness Constraint") {
        auto comp1 = std::make_shared<TestComponent>();
        comp1->value = 1;
        node->AddComponent(comp1);
        
        auto comp2 = std::make_shared<TestComponent>();
        comp2->value = 2;
        node->AddComponent(comp2); // Should replace
        
        auto retrieved = node->GetComponent<TestComponent>();
        REQUIRE(retrieved != nullptr);
        CHECK(retrieved->value == 2);
        CHECK(retrieved == comp2.get());
    }

    SECTION("Get Non-existent Component") {
        auto retrieved = node->GetComponent<TestComponent>();
        CHECK(retrieved == nullptr);
    }

    SECTION("A component has one owner") {
        auto otherNode = Assets::Node::CreateNode(
            "OtherNode", glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1), 1);
        auto component = std::make_shared<TestComponent>();
        node->AddComponent(component);

        CHECK_THROWS_AS(otherNode->AddComponent(component), std::logic_error);
        CHECK(component->GetOwner() == node.get());
        CHECK(node->GetComponent<TestComponent>() == component.get());
        CHECK(otherNode->GetComponent<TestComponent>() == nullptr);
    }
}

TEST_CASE("Node hierarchy uses non-owning parent links", "[Unit][Node][Hierarchy]")
{
    std::weak_ptr<Assets::Node> weakParent;
    std::weak_ptr<Assets::Node> weakChild;
    {
        auto parent = Assets::Node::CreateNode(
            "Parent", glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1), 1);
        auto child = Assets::Node::CreateNode(
            "Child", glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1), 2);
        child->SetParent(parent);

        weakParent = parent;
        weakChild = child;
        CHECK(child->GetParent() == parent.get());
        REQUIRE(parent->Children().size() == 1);
        CHECK_THROWS_AS(parent->SetParent(child), std::logic_error);
    }

    CHECK(weakParent.expired());
    CHECK(weakChild.expired());
}

TEST_CASE_METHOD(EngineTestFixture, "Scene component index follows component and node lifetime",
                 "[GPU][Integration][ComponentIndex]")
{
    Assets::Scene& scene = engine_->GetScene();
    auto firstNode = Assets::Node::CreateNode(
        "IndexedFirst", glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1), scene.GenerateInstanceId());
    auto firstComponent = std::make_shared<TestComponent>();
    firstNode->AddComponent(firstComponent);
    scene.AddNode(firstNode);

    REQUIRE(scene.Components<TestComponent>().size() == 1);
    CHECK(*scene.Components<TestComponent>().begin() == firstComponent.get());
    CHECK(scene.FindNodeIdWithComponent("TestComponent") == static_cast<int32_t>(firstNode->GetInstanceId()));

    auto replacement = std::make_shared<TestComponent>();
    firstNode->AddComponent(replacement);
    REQUIRE(scene.Components<TestComponent>().size() == 1);
    CHECK(*scene.Components<TestComponent>().begin() == replacement.get());
    CHECK(firstComponent->GetOwner() == nullptr);

    auto secondNode = Assets::Node::CreateNode(
        "IndexedSecond", glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1), scene.GenerateInstanceId());
    scene.AddNode(secondNode);
    auto secondComponent = std::make_shared<TestComponent>();
    secondNode->AddComponent(secondComponent);
    CHECK(scene.Components<TestComponent>().size() == 2);

    firstNode->RemoveComponent<TestComponent>();
    REQUIRE(scene.Components<TestComponent>().size() == 1);
    CHECK(*scene.Components<TestComponent>().begin() == secondComponent.get());
    CHECK(replacement->GetOwner() == nullptr);

    const uint32_t secondId = secondNode->GetInstanceId();
    scene.RemoveNodeByInstanceId(secondId);
    CHECK(scene.Components<TestComponent>().empty());
    CHECK(secondComponent->GetOwner() == secondNode.get());

    scene.RemoveNodeByInstanceId(firstNode->GetInstanceId());
}

TEST_CASE_METHOD(EngineTestFixture, "Scene evaluates editable animation tracks at an explicit time",
                 "[GPU][Integration][Animation][Sequencer]")
{
    Assets::Scene& scene = engine_->GetScene();
    auto node = Assets::Node::CreateNode(
        "SequencerTarget", glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f), scene.GenerateInstanceId());
    scene.AddNode(node);

    Assets::AnimationTrack transformTrack;
    transformTrack.AnimationName = "EditorPreview";
    transformTrack.NodeName_ = node->GetName();
    transformTrack.Duration_ = 2.0f;
    transformTrack.TranslationChannel.Keys = {
        {0.0f, glm::vec3(0.0f)},
        {2.0f, glm::vec3(4.0f, 2.0f, -2.0f)},
    };

    Assets::AnimationTrack environmentTrack;
    environmentTrack.AnimationName = "Daylight";
    environmentTrack.Target_ = Assets::AnimationTrack::Target::Environment;
    environmentTrack.Duration_ = 2.0f;
    environmentTrack.SunIntensityChannel.Keys = {{0.0f, 100.0f}, {2.0f, 500.0f}};

    scene.Tracks() = {transformTrack, environmentTrack};
    scene.EvaluateTracks(1.0f);

    CHECK(node->Translation().x == Catch::Approx(2.0f));
    CHECK(node->Translation().y == Catch::Approx(1.0f));
    CHECK(node->Translation().z == Catch::Approx(-1.0f));
    CHECK(scene.GetEnvSettings().SunIntensity == Catch::Approx(300.0f));
    CHECK(scene.Tracks()[0].Time_ == Catch::Approx(1.0f));
    CHECK(scene.Tracks()[1].Time_ == Catch::Approx(1.0f));

    scene.RemoveNodeByInstanceId(node->GetInstanceId());
    scene.Tracks().clear();
}

TEST_CASE("Node velocity tracks rigid rotation", "[Unit][Node][MotionVector]")
{
    auto node = Assets::Node::CreateNode(
        "RotatingNode", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0);

    CHECK_FALSE(node->TickVelocity());

    node->SetRotation(glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    REQUIRE(node->TickVelocity());

    Assets::NodeProxy proxy{};
    node->GetNodeProxy(proxy);
    const glm::mat4 mappedPrevious = proxy.combinedPrevTS * node->WorldTransform();
    const glm::mat4 identity(1.0f);
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            CHECK(mappedPrevious[column][row] == Catch::Approx(identity[column][row]).margin(0.0001f));
        }
    }

    CHECK_FALSE(node->TickVelocity());
    node->GetNodeProxy(proxy);
    CHECK(proxy.combinedPrevTS == identity);
}

TEST_CASE("Environment sun direction supports elevation", "[Unit][Environment]")
{
    Assets::EnvironmentSetting environment;

    const glm::vec3 legacyDirection = glm::normalize(glm::vec3(1.0f, 0.75f, 0.0f));
    CHECK(environment.SunDirection().x == Catch::Approx(legacyDirection.x).margin(0.0001f));
    CHECK(environment.SunDirection().y == Catch::Approx(legacyDirection.y).margin(0.0001f));
    CHECK(environment.SunDirection().z == Catch::Approx(legacyDirection.z).margin(0.0001f));

    environment.SunElevation = glm::radians(-12.0f);
    CHECK(environment.SunDirection().y < 0.0f);
}

TEST_CASE("Animation track samples environment channels", "[Unit][Environment][Animation]")
{
    Assets::EnvironmentSetting environment;
    Assets::AnimationTrack track;
    CHECK(track.Time_ == 0.0f);
    CHECK(track.Duration_ == 0.0f);
    track.Target_ = Assets::AnimationTrack::Target::Environment;
    track.SunRotationChannel.Keys = {{0.0f, -0.5f}, {10.0f, 0.5f}};
    track.SunElevationChannel.Keys = {{0.0f, 0.0f}, {10.0f, glm::half_pi<float>()}};
    track.SunIntensityChannel.Keys = {{0.0f, 0.0f}, {10.0f, 1000.0f}};
    track.SkyIntensityChannel.Keys = {{0.0f, 4.0f}, {10.0f, 84.0f}};
    track.SunColorChannel.Keys = {
        {0.0f, glm::vec3(1.0f, 0.2f, 0.1f)},
        {10.0f, glm::vec3(1.0f, 1.0f, 0.9f)},
    };
    track.SkyColorChannel.Keys = {
        {0.0f, glm::vec3(0.3f, 0.2f, 0.6f)},
        {10.0f, glm::vec3(1.0f, 0.9f, 0.8f)},
    };

    track.Sample(5.0f, environment);

    CHECK(environment.SunRotation == Catch::Approx(0.0f));
    CHECK(environment.SunElevation == Catch::Approx(glm::quarter_pi<float>()));
    CHECK(environment.SunIntensity == Catch::Approx(500.0f));
    CHECK(environment.SkyIntensity == Catch::Approx(44.0f));
    CHECK(environment.SunColor.r == Catch::Approx(1.0f));
    CHECK(environment.SunColor.g == Catch::Approx(0.6f));
    CHECK(environment.SunColor.b == Catch::Approx(0.5f));
    CHECK(environment.SkyColor.r == Catch::Approx(0.65f));
    CHECK(environment.SkyColor.g == Catch::Approx(0.55f));
    CHECK(environment.SkyColor.b == Catch::Approx(0.7f));
}

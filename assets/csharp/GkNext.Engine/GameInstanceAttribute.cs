namespace GkNext;

/// <summary>
/// Marks the type the engine should instantiate as this assembly's game module.
/// </summary>
/// <remarks>
/// A source generator turns this into a registry that names the type directly, because the entry
/// point has to be resolvable at compile time: NativeAOT has no assembly scanning, and anything
/// that looks like <c>AppDomain.GetAssemblies()</c> would work under CoreCLR and then fail in
/// release (design section 3.4 rule 3).
///
/// Exactly one type per game assembly may carry this attribute.
/// </remarks>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class GameInstanceAttribute : Attribute
{
}

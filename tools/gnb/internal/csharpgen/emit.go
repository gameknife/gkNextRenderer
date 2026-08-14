package csharpgen

import (
	"fmt"
	"strings"
)

// rawTypes maps the C++ types allowed in the def file to their blittable C# counterparts. A type
// that is not in this table is rejected at parse time — that is deliberate, because the failure
// mode of a wrong mapping is silent memory corruption under one backend only.
var rawTypes = map[string]string{
	"void":            "void",
	"float":           "float",
	"double":          "double",
	"int32_t":         "int",
	"uint32_t":        "uint",
	"uint8_t":         "byte",
	"char":            "byte",
	"GkStr":           "GkStr",
	"GkBool":          "int",
	"GkColor32":       "uint",
	"FVec2":           "Vector2",
	"FVec3":           "Vector3",
	"FVec4":           "Vector4",
	"FRenderNodeSpec": "RenderNodeSpec",
	"FCameraOverride": "CameraOverride",
}

func rawType(param Param) string {
	mapped, ok := rawTypes[param.Base]
	if !ok {
		return param.Base
	}
	if param.IsPtr {
		return mapped + "*"
	}
	return mapped
}

// friendlyParam is one parameter of the generated wrapper, after the raw signature has been
// reshaped into something a game author would want to call.
type friendlyParam struct {
	Declaration string // e.g. "string name", "in Vector3 color"
	Default     string // C# default value, empty when there is none
	Argument    string // expression passed to the raw call
	NeedsArena  bool   // parameter is a string and must be encoded into the UTF-8 arena
	PinFrom     string // when set, the managed value to pin with `fixed`
	PinType     string
	PinName     string
}

// plan is the fully classified form of an entry, ready to emit.
type plan struct {
	Entry      Entry
	Params     []friendlyParam
	ReturnType string
	// exactly one of these describes how the return value is produced
	OutVectorName string // out-parameter that becomes the return value
	OutVectorType string
	StringBuffer  bool // (char* buffer, int32_t capacity) pair returning a string
	ByteBuffer    bool // (uint8_t* buffer, int32_t capacity) pair returning a byte[]
	BoolReturn    bool
}

func buildPlan(entry Entry) (plan, error) {
	result := plan{Entry: entry}

	for index := 0; index < len(entry.Params); index++ {
		param := entry.Params[index]

		// Buffer-and-capacity pairs collapse into a managed return value.
		if param.IsPtr && (param.Base == "char" || param.Base == "uint8_t") {
			if index+1 >= len(entry.Params) || entry.Params[index+1].Base != "int32_t" {
				return result, fmt.Errorf("%s: buffer parameter %q must be followed by an int32_t capacity",
					entry.Symbol(), param.Name)
			}
			if param.Base == "char" {
				result.StringBuffer = true
			} else {
				result.ByteBuffer = true
			}
			index++ // consume the capacity parameter too
			continue
		}

		// An out-vector becomes the return value; C# callers should not have to declare a local.
		if param.IsPtr && !param.IsConst && strings.HasPrefix(param.Name, "out") {
			if result.OutVectorName != "" {
				return result, fmt.Errorf("%s: more than one out-parameter", entry.Symbol())
			}
			result.OutVectorName = param.Name
			result.OutVectorType = rawTypes[param.Base]
			continue
		}

		friendly, err := buildParam(entry, param)
		if err != nil {
			return result, err
		}
		result.Params = append(result.Params, friendly)
	}

	switch {
	case result.OutVectorName != "":
		result.ReturnType = result.OutVectorType
	case result.StringBuffer:
		result.ReturnType = "string"
	case result.ByteBuffer:
		result.ReturnType = "byte[]"
	case entry.Return == "GkBool":
		result.ReturnType = "bool"
		result.BoolReturn = true
	default:
		mapped, ok := rawTypes[entry.Return]
		if !ok {
			return result, fmt.Errorf("%s: unknown return type %q", entry.Symbol(), entry.Return)
		}
		result.ReturnType = mapped
	}

	// C# requires optional parameters to be trailing.
	seenDefault := false
	for _, param := range result.Params {
		if param.Default != "" {
			seenDefault = true
			continue
		}
		if seenDefault {
			return result, fmt.Errorf("%s: parameters with defaults must come last", entry.Symbol())
		}
	}

	return result, nil
}

func buildParam(entry Entry, param Param) (friendlyParam, error) {
	result := friendlyParam{}
	defaultValue, hasDefault := entry.Defaults[param.Name]

	switch {
	case param.Base == "GkStr":
		result.Declaration = "string " + param.Name
		result.Argument = "Utf8Arena.Encode(" + param.Name + ")"
		result.NeedsArena = true

	case param.Base == "GkBool":
		result.Declaration = "bool " + param.Name
		result.Argument = param.Name + " ? 1 : 0"
		if hasDefault {
			switch defaultValue {
			case "0", "false":
				defaultValue = "false"
			case "1", "true":
				defaultValue = "true"
			default:
				return result, fmt.Errorf("%s: parameter %q default %q is not a bool",
					entry.Symbol(), param.Name, defaultValue)
			}
		}

	case param.Base == "GkColor32":
		result.Declaration = "Color " + param.Name
		result.Argument = param.Name + ".ToPacked()"

	case param.IsPtr && param.IsConst:
		managed := rawTypes[param.Base]
		result.Declaration = "in " + managed + " " + param.Name
		result.PinFrom = param.Name
		result.PinType = managed
		result.PinName = "p_" + param.Name
		result.Argument = result.PinName

	default:
		result.Declaration = rawTypes[param.Base] + " " + param.Name
		result.Argument = param.Name
	}

	if hasDefault {
		result.Default = defaultValue
	}
	return result, nil
}

// Generate renders the complete Engine.g.cs for the given entries.
func Generate(entries []Entry) (string, error) {
	var out strings.Builder

	out.WriteString(`// <auto-generated>
//     Generated by 'gnb csharpgen' from src/Modules/NextDotNet/EngineApi.def.h.
//     Do not edit. Add a binding by adding a line to the def file and regenerating.
//     'gnb csharpgen --check' fails when this file is out of date.
// </auto-generated>

using System.Runtime.InteropServices;
using GkNext.Interop;

namespace GkNext.Interop
{
    /// <summary>
    /// Native services handed to managed code at bootstrap. Layout must match FEngineApi in
    /// src/Modules/NextDotNet/Interop.h exactly; both are expanded from the same def file.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct FEngineApi
    {
        public uint Version;

`)

	for _, entry := range entries {
		signature := make([]string, 0, len(entry.Params)+1)
		for _, param := range entry.Params {
			signature = append(signature, rawType(param))
		}
		returnType := "void"
		if entry.Return != "void" {
			if mapped, ok := rawTypes[entry.Return]; ok {
				returnType = mapped
			} else {
				return "", fmt.Errorf("%s: unknown return type %q", entry.Symbol(), entry.Return)
			}
		}
		signature = append(signature, returnType)
		fmt.Fprintf(&out, "        public delegate* unmanaged<%s> %s;\n", strings.Join(signature, ", "), entry.Symbol())
	}

	fmt.Fprintf(&out, `    }

    /// <summary>Number of bindings in the table, checked against the native side.</summary>
    public static class EngineApiInfo
    {
        public const int EntryCount = %d;
    }
}

namespace GkNext
{
`, len(entries))

	// Group into one static class per def-file namespace, preserving first-appearance order.
	var order []string
	grouped := map[string][]Entry{}
	for _, entry := range entries {
		if _, seen := grouped[entry.Namespace]; !seen {
			order = append(order, entry.Namespace)
		}
		grouped[entry.Namespace] = append(grouped[entry.Namespace], entry)
	}

	for index, namespace := range order {
		if index > 0 {
			out.WriteString("\n")
		}
		fmt.Fprintf(&out, "    public static unsafe class %s\n    {\n", namespace)
		for entryIndex, entry := range grouped[namespace] {
			if entryIndex > 0 {
				out.WriteString("\n")
			}
			body, err := emitMethod(entry)
			if err != nil {
				return "", err
			}
			out.WriteString(body)
		}
		out.WriteString("    }\n")
	}

	out.WriteString("}\n")
	return out.String(), nil
}

func emitMethod(entry Entry) (string, error) {
	built, err := buildPlan(entry)
	if err != nil {
		return "", err
	}

	declarations := make([]string, 0, len(built.Params))
	for _, param := range built.Params {
		declaration := param.Declaration
		if param.Default != "" {
			declaration += " = " + param.Default
		}
		declarations = append(declarations, declaration)
	}

	var out strings.Builder
	fmt.Fprintf(&out, "        public static %s %s(%s)\n        {\n",
		built.ReturnType, entry.Name, strings.Join(declarations, ", "))

	indent := "            "
	needsArena := false
	for _, param := range built.Params {
		if param.NeedsArena {
			needsArena = true
		}
	}

	if needsArena {
		out.WriteString(indent + "var mark = Utf8Arena.Mark();\n")
		out.WriteString(indent + "try\n" + indent + "{\n")
		indent += "    "
	}

	closePins := 0
	for _, param := range built.Params {
		if param.PinFrom == "" {
			continue
		}
		fmt.Fprintf(&out, "%sfixed (%s* %s = &%s)\n%s{\n",
			indent, param.PinType, param.PinName, param.PinFrom, indent)
		indent += "    "
		closePins++
	}

	arguments := make([]string, 0, len(entry.Params))
	for _, param := range built.Params {
		arguments = append(arguments, param.Argument)
	}

	switch {
	case built.OutVectorName != "":
		fmt.Fprintf(&out, "%s%s result = default;\n", indent, built.OutVectorType)
		call := append(append([]string{}, arguments...), "&result")
		fmt.Fprintf(&out, "%sApi.Table->%s(%s);\n", indent, entry.Symbol(), strings.Join(call, ", "))
		fmt.Fprintf(&out, "%sreturn result;\n", indent)

	case built.StringBuffer, built.ByteBuffer:
		// Probe for the required size, then fill. The engine reports the length it would have
		// written when handed a null buffer, so one convention covers both calls.
		probe := append(append([]string{}, arguments...), "null", "0")
		fmt.Fprintf(&out, "%sint required = Api.Table->%s(%s);\n", indent, entry.Symbol(), strings.Join(probe, ", "))
		empty := "string.Empty"
		if built.ByteBuffer {
			empty = "System.Array.Empty<byte>()"
		}
		fmt.Fprintf(&out, "%sif (required <= 0)\n%s{\n%s    return %s;\n%s}\n", indent, indent, indent, empty, indent)
		fmt.Fprintf(&out, "%sbyte[] buffer = new byte[required];\n", indent)
		fmt.Fprintf(&out, "%sfixed (byte* bufferPtr = buffer)\n%s{\n", indent, indent)
		fill := append(append([]string{}, arguments...), "bufferPtr", "required")
		fmt.Fprintf(&out, "%s    int written = Api.Table->%s(%s);\n", indent, entry.Symbol(), strings.Join(fill, ", "))
		if built.StringBuffer {
			fmt.Fprintf(&out, "%s    return written <= 0 ? string.Empty : System.Text.Encoding.UTF8.GetString(bufferPtr, written);\n", indent)
		} else {
			fmt.Fprintf(&out, "%s    if (written == required)\n%s    {\n%s        return buffer;\n%s    }\n", indent, indent, indent, indent)
			fmt.Fprintf(&out, "%s    return written <= 0 ? System.Array.Empty<byte>() : buffer.AsSpan(0, written).ToArray();\n", indent)
		}
		fmt.Fprintf(&out, "%s}\n", indent)

	case built.ReturnType == "void":
		fmt.Fprintf(&out, "%sApi.Table->%s(%s);\n", indent, entry.Symbol(), strings.Join(arguments, ", "))

	case built.BoolReturn:
		fmt.Fprintf(&out, "%sreturn Api.Table->%s(%s) != 0;\n", indent, entry.Symbol(), strings.Join(arguments, ", "))

	default:
		fmt.Fprintf(&out, "%sreturn Api.Table->%s(%s);\n", indent, entry.Symbol(), strings.Join(arguments, ", "))
	}

	for i := 0; i < closePins; i++ {
		indent = indent[:len(indent)-4]
		out.WriteString(indent + "}\n")
	}

	if needsArena {
		indent = indent[:len(indent)-4]
		out.WriteString(indent + "}\n")
		out.WriteString(indent + "finally\n" + indent + "{\n")
		out.WriteString(indent + "    Utf8Arena.Release(mark);\n")
		out.WriteString(indent + "}\n")
	}

	out.WriteString("        }\n")
	return out.String(), nil
}

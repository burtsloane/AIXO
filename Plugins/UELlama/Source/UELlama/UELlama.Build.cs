using UnrealBuildTool;
using System.IO; // Required for Path.Combine

public class UELlama : ModuleRules // Ensure this class name matches your actual file
{
    public UELlama(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // Standard Unreal Engine module dependencies
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Projects",   // Often needed for plugin utilities
            "UMG",        // For UUserWidget and UMG functionalities
            "Slate",      // For SWidget and higher-level Slate functionalities
            "SlateCore"   // For core Slate types like SLeafWidget, EVisibility, brushes, etc.
        });

        // Private dependencies (if any specific to the plugin's implementation details)
        PrivateDependencyModuleNames.AddRange(new string[] {
            // "Slate", "SlateCore", // Add if your plugin uses Slate UI directly
        });
//		PrivateDependencyModuleNames.AddRange(new string[] { "AIXO" /* Add your game module name here */ });
		PrivateIncludePathModuleNames.AddRange(new string[] { "AIXO" });

        // Define the base path to your ThirdParty llama.cpp directory
        string ThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty");
        string LlamaCppSourcePath = Path.Combine(ThirdPartyPath, "llama.cpp"); // Root of the llama.cpp source/headers

        // --- Include Paths for llama.cpp headers ---
        // This allows your plugin's .cpp files to #include "llama.h", "ggml.h", etc.
        PublicIncludePaths.Add(Path.Combine(LlamaCppSourcePath, "include")); // Assuming headers are in ThirdParty/llama.cpp/include/
        // If common.h/cpp are in a subfolder like llama.cpp/common/ and you put them in include/common/
        // PublicIncludePaths.Add(Path.Combine(LlamaCppSourcePath, "include", "common"));


        // --- Platform-Specific Settings ---
        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            // Path to the precompiled static library for macOS
            string LlamaLibDir = Path.Combine(LlamaCppSourcePath, "lib", "Mac"); // e.g., ThirdParty/llama.cpp/lib/Mac/
            PublicAdditionalLibraries.Add(Path.Combine(LlamaLibDir, "libllama.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LlamaLibDir, "libggml.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LlamaLibDir, "libggml-cpu.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LlamaLibDir, "libggml-base.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LlamaLibDir, "libggml-metal.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LlamaLibDir, "libcommon.a"));

    string PluginResourcesPath = Path.Combine(ModuleDirectory, "..", "Resources"); // Goes up one level from Source/UELlama to UELlama-main, then into Resources
    if (Directory.Exists(PluginResourcesPath))
    {
//        RuntimeDependencies.Add("$(PluginDir)/Resources/...", Path.Combine(PluginResourcesPath, "..."), StagedFileType.SystemNonUFS);
        // More specifically for a single file:
        RuntimeDependencies.Add(Path.Combine("$(PluginDir)/Resources/ggml-metal.metal"), Path.Combine(PluginResourcesPath, "ggml-metal.metal"), StagedFileType.SystemNonUFS);
        // For UE5.1+, a cleaner way to stage plugin resources:
        // Add এটাকে ("$(PluginDir)/Resources/ggml-metal.metal", Path.Combine(FPaths::ProjectPluginsDir(), "UELlama-main", "Resources", "ggml-metal.metal"));
    }







    string PluginResourcesDir = Path.Combine(PluginDirectory, "Resources"); // PluginDirectory is a ModuleRules property
    string MetalShaderSourcePath = Path.Combine(PluginResourcesDir, "ggml-metal.metal");

    if (File.Exists(MetalShaderSourcePath))
    {
        // This attempts to stage it to YourGame.app/Contents/MacNoEditor/YourProjectName/Plugins/UELlama/Resources/ggml-metal.metal
        // RuntimeDependencies.Add(MetalShaderSourcePath, StagedFileType.UFS); // UFS for files that UE's file system might manage

        // To try and get it into the main app bundle's Contents/Resources, which NSBundle typically searches:
        // This is a bit more of a direct copy instruction.
        // The destination path is relative to the packaged app's root (where Contents is).
        RuntimeDependencies.Add("$(EngineDir)/Intermediate/NoRedist/ggml-metal.metal", MetalShaderSourcePath, StagedFileType.SystemNonUFS);
        // The first argument to RuntimeDependencies.Add is often a "virtual" path used by UBT during staging,
        // the second is the actual source on disk. StagedFileType.SystemNonUFS is for files not part of UE's asset system.
        // This is still a bit experimental to force it into the main Contents/Resources.

        // A potentially more reliable way for plugins to add to the main app bundle's resources
        // might involve custom build steps or ensuring the plugin's resources are correctly
        // identified by the macOS bundling process.

        // For UE 5.1+ there's a cleaner way to copy to bundle resources:
        // This copies it to YourGame.app/Contents/Resources/ggml-metal.metal
        AdditionalBundleResources.Add(new BundleResource(MetalShaderSourcePath));
    }
    else
    {
        System.Console.WriteLine("UELlama WARNING: ggml-metal.metal not found at: " + MetalShaderSourcePath);
    }







            // Definitions that might be required by llama.h or your C++ code to enable Metal features
            PublicDefinitions.Add("GGML_USE_METAL=1"); // Or just GGML_USE_METAL, check llama.cpp source
            PublicDefinitions.Add("GGML_USE_ACCELERATE=1"); // If you built llama.cpp with Accelerate support too

            // Link against necessary system frameworks for Metal and Accelerate
            PublicFrameworks.AddRange(new string[] {
                "Metal",
                "Foundation",
                "MetalKit",
                "Accelerate"
            });
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Example for Windows (you'd need to compile llama.lib with appropriate settings, e.g., CUDA)
            // string LlamaLibDir = Path.Combine(LlamaCppSourcePath, "lib", "Win64");
            // PublicAdditionalLibraries.Add(Path.Combine(LlamaLibDir, "llama.lib"));
            // PublicDefinitions.Add("GGML_USE_CUBLAS=1"); // If built with CUDA
            // Add paths to CUDA toolkit if not in system path, etc.
            // DelayLoadDLLs.Add("nvcuda.dll"); // Example
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            // Example for Linux
            // string LlamaLibDir = Path.Combine(LlamaCppSourcePath, "lib", "Linux");
            // PublicAdditionalLibraries.Add(Path.Combine(LlamaLibDir, "libllama.a"));
            // PublicDefinitions.Add("GGML_USE_CLBLAST=1"); // If built with OpenCL/CLBlast
            // Or: PublicDefinitions.Add("GGML_USE_OPENBLAS=1"); // If built with OpenBLAS
            // PublicSystemLibraries.Add("openblas"); // If linking system OpenBLAS
        }

        // If you need to compile llama.cpp source files directly (more complex, less recommended for llama.cpp)
        // You would uncomment and adapt something like this, listing ALL .c and .cpp files:
        /*
        if (Target.bCompileमाजcpp == true) // Custom flag you could set in Target.cs
        {
            PrivateIncludePaths.Add(LlamaCppSourcePath); // llama.cpp's own root for its internal includes
            if (Directory.Exists(Path.Combine(LlamaCppSourcePath, "common"))) {
                PrivateIncludePaths.Add(Path.Combine(LlamaCppSourcePath, "common"));
            }

            List<string> SourceFiles = new List<string>();
            SourceFiles.Add(Path.Combine(LlamaCppSourcePath, "llama.cpp"));
            SourceFiles.Add(Path.Combine(LlamaCppSourcePath, "ggml.c"));
            SourceFiles.Add(Path.Combine(LlamaCppSourcePath, "ggml-alloc.c"));
            SourceFiles.Add(Path.Combine(LlamaCppSourcePath, "ggml-backend.c"));
            if (Target.Platform == UnrealTargetPlatform.Mac) {
                SourceFiles.Add(Path.Combine(LlamaCppSourcePath, "ggml-metal.m")); // Note the .m
            }
            // ... Add ALL other necessary .c and .cpp files from llama.cpp ...
            // This list can be long and hard to maintain.

            PrivateDefinitions.AddRange(new string[] {
                // Definitions needed for compiling llama.cpp source directly
                // "GGML_STATIC", // Often needed if compiling as part of another project
                // "_GNU_SOURCE", // Sometimes for Linux
            });

            // This part is highly dependent on how the mika314/UELlama plugin
            // was originally intended to integrate the source.
            // For direct source compilation, you might also need to modify CStandard and CppStandard:
            // CStandard = CStandardVersion.C11;
            // CppStandard = CppStandardVersion.Cpp17;
        }
        */

        // Suppress specific warnings if headers from llama.cpp generate them and are noisy
        // bEnableUndefinedIdentifierWarnings = false;
    }
}




/*
// Copyright (c) 2022 Mika Pi

using UnrealBuildTool;
using System.IO;

public class UELlama : ModuleRules
{
	public UELlama(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...
			}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd"
				}
			);
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicAdditionalLibraries.Add(Path.Combine(PluginDirectory, "Libraries", "libllama.so"));
			PublicIncludePaths.Add(Path.Combine(PluginDirectory, "Includes"));
		} else if (Target.Platform == UnrealTargetPlatform.Win64) {
			PublicAdditionalLibraries.Add(Path.Combine(PluginDirectory, "Libraries", "llama.lib"));
            PublicIncludePaths.Add(Path.Combine(PluginDirectory, "Includes"));
		}

	}
}
*/

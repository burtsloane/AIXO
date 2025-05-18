using UnrealBuildTool;

public class AIXO : ModuleRules
{
	public AIXO(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		// Configure for monolithic builds
		bUsePrecompiled = false;  // Ensure we're not using precompiled
		bEnableUndefinedIdentifierWarnings = false;  // Reduce noise in monolithic builds
	
		// Allow this module to be included in precompiled builds
		PrecompileForTargets = PrecompileTargetsType.Any;

		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
			// Add necessary modules here
			"UMG", 
			"Slate", 
			"SlateCore",
			"Json",
            "JsonUtilities",
            "EnhancedInput",
            "UELlama"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
} 

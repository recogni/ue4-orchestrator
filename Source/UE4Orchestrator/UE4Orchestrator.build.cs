using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;

public class UE4Orchestrator : ModuleRules
{
#if WITH_FORWARDED_MODULE_RULES_CTOR
    public UE4Orchestrator(ReadOnlyTargetRules Target) : base(Target)
#else
    public UE4Orchestrator(TargetInfo Target)
#endif // WITH_FORWARDED_MODULE_RULES_CTOR
    {
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Sockets",
                "StreamingFile",
                "LevelEditor",
                "NetworkFile",
                "PakFile",
                "InputCore",
                "Json",
                "JsonUtilities",
                "AssetTools",
                "HTTP",
                // ... add private dependencies that you statically link with here ...
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject", // @todo Mac: for some reason it's needed to link in debug on Mac
		"Engine",
                "PakFile",
                "Sockets",
                "StreamingFile",
                "LevelEditor",
                "NetworkFile",
                "InputCore",
                "Json",
                "JsonUtilities",
                "AssetTools",
            }
        );

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
            PublicDependencyModuleNames.Add("UnrealEd");
        }
    }
}

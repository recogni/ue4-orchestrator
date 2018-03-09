using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;

public class UE4Orchestrator : ModuleRules
{
    public UE4Orchestrator(TargetInfo Target)
    {
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Sockets",
                "StreamingFile",
                "LevelEditor",
                "NetworkFile",
                "PakFile",
                "InputCore",
                "AssetTools",
                "HTTP",
                "UnrealEd",
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
                "AssetTools",
                "UnrealEd",
            }
        );
    }
}

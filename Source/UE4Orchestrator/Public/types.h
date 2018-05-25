/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; -*- */

#pragma once

// UE4
#include "IPlatformFilePak.h"
#include "FileManagerGeneric.h"
#include "StreamingNetworkPlatformFile.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"

#if WITH_EDITOR
#  include "LevelEditor.h"
#  include "Editor.h"
#  include "Editor/LevelEditor/Public/ILevelViewport.h"
#  include "Editor/LevelEditor/Public/LevelEditorActions.h"
#  include "Editor/UnrealEd/Public/LevelEditorViewport.h"
#endif

////////////////////////////////////////////////////////////////////////////////

typedef struct mg_str               mg_str_t;
typedef struct http_message         http_message_t;
typedef FModuleManager              FManager;
typedef FActorComponentTickFunction FTickFn;

#if WITH_EDITOR
  typedef FLevelEditorModule        FLvlEditor;
#endif

////////////////////////////////////////////////////////////////////////////////

#define T                   TEXT
#define LOG(fmt, ...)       UE_LOG(LogUE4Orc, Log, TEXT(fmt), __VA_ARGS__)

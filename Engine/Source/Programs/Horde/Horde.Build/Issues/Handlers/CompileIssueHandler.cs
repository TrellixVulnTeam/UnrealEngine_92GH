// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using EpicGames.Core;
using Horde.Build.Jobs;
using Horde.Build.Jobs.Graphs;
using Horde.Build.Logs;
using Microsoft.Extensions.Logging;
using MongoDB.Driver;

namespace Horde.Build.Issues.Handlers
{
	/// <summary>
	/// Instance of a particular compile error
	/// </summary>
	class CompileIssueHandler : SourceFileIssueHandler
	{
		/// <summary>
		/// Annotation describing the compile type
		/// </summary>
		const string CompileTypeAnnotation = "CompileType";

		/// <inheritdoc/>
		public override string Type => "Compile";

		/// <inheritdoc/>
		public override int Priority => 10;

		/// <summary>
		/// Determines if the given event id matches
		/// </summary>
		/// <param name="eventId">The event id to compare</param>
		/// <returns>True if the given event id matches</returns>
		public static bool IsMatchingEventId(EventId? eventId)
		{
			return eventId == KnownLogEvents.Compiler || eventId == KnownLogEvents.AutomationTool_SourceFileLine || eventId == KnownLogEvents.MSBuild;
		}

		/// <inheritdoc/>
		public override void TagEvents(IJob job, INode node, IReadOnlyNodeAnnotations annotations, IReadOnlyList<IssueEvent> stepEvents)
		{
			foreach (IssueEvent stepEvent in stepEvents)
			{
				if (stepEvent.EventId.HasValue && IsMatchingEventId(stepEvent.EventId))
				{
					List<string> newFileNames = new List<string>();
					GetSourceFiles(stepEvent.EventData, newFileNames);

					string? compileType;
					if (!annotations.TryGetValue(CompileTypeAnnotation, out compileType))
					{
						compileType = "Compile";
					}

					List<string> newMetadata = new List<string>();
					newMetadata.Add($"{CompileTypeAnnotation}={compileType}");

					stepEvent.Fingerprint = new NewIssueFingerprint(Type, newFileNames, null, newMetadata);
				}
			}
		}

		/// <inheritdoc/>
		public override string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity)
		{
			List<string> types = fingerprint.GetMetadataValues(CompileTypeAnnotation).ToList();
			string type = (types.Count == 1) ? types[0] : "Compile";
			string level = (severity == IssueSeverity.Warning) ? "warnings" : "errors";
			string list = StringUtils.FormatList(fingerprint.Keys.Where(x => !x.StartsWith(NotePrefix, StringComparison.Ordinal)).ToArray(), 2);
			return $"{type} {level} in {list}";
		}
	}
}

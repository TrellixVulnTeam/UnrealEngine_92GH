// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Collections;
using Horde.Build.Config;
using Horde.Build.Models;
using Horde.Build.Notifications;
using Horde.Build.Server;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Issues.Impl
{
	using TemplateRefId = StringId<TemplateRef>;
	using WorkflowId = StringId<WorkflowConfig>;

#pragma warning disable CS1591 // Missing XML comment for publicly visible type or member
	public class WorkflowStats
	{
		public int NumSteps { get; set; }
		public int NumPassingSteps { get; set; }
	}

	public class IssueReport
	{
		public DateTimeOffset Time { get; }
		public IStream Stream { get; }
		public WorkflowConfig Workflow { get; }
		public WorkflowStats WorkflowStats { get; }
		public List<IIssue> Issues { get; } = new List<IIssue>();
		public List<IIssueSpan> IssueSpans { get; } = new List<IIssueSpan>();

		public IssueReport(DateTimeOffset time, IStream stream, WorkflowConfig workflow, WorkflowStats workflowStats)
		{
			Time = time;
			Stream = stream;
			Workflow = workflow;
			WorkflowStats = workflowStats;
		}
	}
#pragma warning restore CS1591 // Missing XML comment for publicly visible type or member

	[SingletonDocument("6268871c211d05611b3e4fd8")]
	class IssueReportState : SingletonBase
	{
		public Dictionary<string, DateTime> KeyToLastReportTime { get; set; } = new Dictionary<string, DateTime>();
	}

	/// <summary>
	/// Posts summaries for all the open issues in different streams to Slack channels
	/// </summary>
	public class IssueReportService : IHostedService
	{
		readonly SingletonDocument<IssueReportState> _state;
		readonly IStreamCollection _streamCollection;
		readonly IIssueCollection _issueCollection;
		readonly IGraphCollection _graphCollection;
		readonly IJobCollection _jobCollection;
		readonly ConfigCollection _configCollection;
		readonly INotificationService _notificationService;
		readonly IClock _clock;
		readonly ITicker _ticker;
		readonly ILogger<IssueReportService> _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public IssueReportService(MongoService mongoService, IStreamCollection streamCollection, IIssueCollection issueCollection, IGraphCollection graphCollection, IJobCollection jobCollection, ConfigCollection configCollection, INotificationService notificationService, IClock clock, ILogger<IssueReportService> logger)
		{
			_state = new SingletonDocument<IssueReportState>(mongoService);
			_streamCollection = streamCollection;
			_issueCollection = issueCollection;
			_graphCollection = graphCollection;
			_jobCollection = jobCollection;
			_configCollection = configCollection;
			_notificationService = notificationService;
			_clock = clock;
			_ticker = clock.AddSharedTicker<IssueReportService>(TimeSpan.FromMinutes(5.0), TickAsync, logger);
			_logger = logger;
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken cancellationToken)
		{
			await _ticker.StartAsync();
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			await _ticker.StopAsync();
		}

		async ValueTask TickAsync(CancellationToken cancellationToken)
		{
			IssueReportState state = await _state.GetAsync();
			HashSet<string> invalidKeys = new HashSet<string>(state.KeyToLastReportTime.Keys, StringComparer.Ordinal);

			DateTime currentTime = _clock.UtcNow;

			List<IStream> streams = await _streamCollection.FindAllAsync();
			foreach (IStream stream in streams)
			{
				StreamConfig config = await _configCollection.GetConfigAsync<StreamConfig>(stream.ConfigRevision);
				if (config.Workflows.Count > 0)
				{
					List<IIssue>? issues = null;
					List<IIssueSpan>? spans = null;

					foreach (WorkflowConfig workflow in config.Workflows)
					{
						string key = $"{stream.Id}:{workflow.Id}";
						invalidKeys.Remove(key);

						DateTime lastReportTime;
						if (!state.KeyToLastReportTime.TryGetValue(key, out lastReportTime))
						{
							state = await _state.UpdateAsync(s => s.KeyToLastReportTime[key] = currentTime);
							continue;
						}

						DateTime lastScheduledReportTime = GetLastScheduledReportTime(workflow, currentTime);
						if (lastReportTime > lastScheduledReportTime)
						{
							continue;
						}

						DateTime prevScheduledReportTime = GetLastScheduledReportTime(workflow, lastScheduledReportTime - TimeSpan.FromMinutes(1.0));

						_logger.LogInformation("Creating report for {StreamId} workflow {WorkflowId}", stream.Id, workflow.Id);

						issues ??= await _issueCollection.FindIssuesAsync(streamId: stream.Id);
						spans ??= await _issueCollection.FindSpansAsync(issueIds: issues.Select(x => x.Id).ToArray());

						Dictionary<WorkflowId, WorkflowStats> workflowIdToStats = await GetWorkflowStatsAsync(stream, prevScheduledReportTime);
						if (!workflowIdToStats.TryGetValue(workflow.Id, out WorkflowStats? workflowStats))
						{
							workflowStats = new WorkflowStats();
						}

						IssueReport report = new IssueReport(currentTime, stream, workflow, workflowStats);
						foreach (IIssueSpan span in spans)
						{
							if (span.LastSuccess != null && span.LastFailure.Annotations.WorkflowId == workflow.Id)
							{
								report.IssueSpans.Add(span);
							}
						}

						HashSet<int> issueIds = new HashSet<int>(report.IssueSpans.Select(x => x.IssueId));
						report.Issues.AddRange(issues.Where(x => issueIds.Contains(x.Id)));

						_notificationService.SendIssueReport(report);

						state = await _state.UpdateAsync(s => s.KeyToLastReportTime[key] = currentTime);
					}
				}
			}

			if (invalidKeys.Count > 0)
			{
				void RemoveInvalidKeys(IssueReportState state)
				{
					foreach (string invalidKey in invalidKeys)
					{
						state.KeyToLastReportTime.Remove(invalidKey);
					}
				}
				state = await _state.UpdateAsync(RemoveInvalidKeys);
			}
		}

		private async Task<Dictionary<WorkflowId, WorkflowStats>> GetWorkflowStatsAsync(IStream stream, DateTime minTime)
		{
			List<IJob> jobs = await _jobCollection.FindAsync(streamId: stream.Id, minCreateTime: minTime);

			Dictionary<WorkflowId, WorkflowStats> workflowIdToStats = new Dictionary<WorkflowId, WorkflowStats>();
			foreach (IGrouping<TemplateRefId, IJob> templateGroup in jobs.GroupBy(x => x.TemplateId))
			{
				WorkflowId? templateWorkflowId = null;
				if (stream.Config.TryGetTemplate(templateGroup.Key, out Api.TemplateRefConfig? templateRefConfig))
				{
					templateWorkflowId = templateRefConfig.Annotations.WorkflowId;
				}

				foreach (IGrouping<ContentHash, IJob> graphGroup in templateGroup.GroupBy(x => x.GraphHash))
				{
					IGraph graph = await _graphCollection.GetAsync(graphGroup.Key);
					foreach (IJob job in graphGroup)
					{
						foreach (IJobStepBatch batch in job.Batches)
						{
							foreach (IJobStep step in batch.Steps)
							{
								if (step.State == JobStepState.Completed)
								{
									INode node = graph.Groups[batch.GroupIdx].Nodes[step.NodeIdx];
									WorkflowId? workflowId = node.Annotations.WorkflowId ?? templateWorkflowId;
									if (workflowId != null)
									{
										WorkflowStats? stats;
										if (!workflowIdToStats.TryGetValue(workflowId.Value, out stats))
										{
											stats = new WorkflowStats();
											workflowIdToStats.Add(workflowId.Value, stats);
										}

										stats.NumSteps++;
										if (step.Outcome == JobStepOutcome.Success)
										{
											stats.NumPassingSteps++;
										}
									}
								}
							}
						}
					}
				}
			}

			return workflowIdToStats;
		}

		static DateTime GetLastScheduledReportTime(WorkflowConfig workflow, DateTime currentTime)
		{
			if (workflow.ReportTimes.Count > 0)
			{
				DateTime startOfDay = currentTime - currentTime.TimeOfDay;
				for (; ; )
				{
					for (int idx = workflow.ReportTimes.Count - 1; idx >= 0; idx--)
					{
						DateTime reportTime = startOfDay + workflow.ReportTimes[idx];
						if (reportTime < currentTime)
						{
							return reportTime;
						}
					}
					startOfDay -= TimeSpan.FromDays(1.0);
				}
			}
			return DateTime.MinValue;
		}
	}
}

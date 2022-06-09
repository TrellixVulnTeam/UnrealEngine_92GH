// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Amazon;
using Amazon.EC2;
using Amazon.EC2.Model;
using Horde.Build.Collections;
using Horde.Build.Models;
using Horde.Build.Utilities;
using Microsoft.Extensions.Logging;
using OpenTracing;
using OpenTracing.Util;

namespace Horde.Build.Services.Impl
{
	/// <summary>
	/// Fleet manager for handling AWS EC2 instances
	/// </summary>
	public sealed class AwsFleetManager : IFleetManager, IDisposable
	{
		const string AwsTagPropertyName = "aws-tag";
		const string PoolTagName = "Horde_Autoscale_Pool";
		readonly AmazonEC2Client _client;
		readonly IAgentCollection _agentCollection;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public AwsFleetManager(IAgentCollection agentCollection, ILogger<AwsFleetManager> logger)
		{
			_agentCollection = agentCollection;
			_logger = logger;

			AmazonEC2Config config = new AmazonEC2Config();
			config.RegionEndpoint = RegionEndpoint.USEast1;

			logger.LogInformation("Initializing AWS fleet manager for region {Region}", config.RegionEndpoint);

			_client = new AmazonEC2Client(config);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_client.Dispose();
		}

		/// <inheritdoc/>
		public async Task ExpandPoolAsync(IPool pool, IReadOnlyList<IAgent> agents, int count)
		{
			using IScope scope = GlobalTracer.Instance.BuildSpan("ExpandPool").StartActive();
			scope.Span.SetTag("poolName", pool.Name);
			scope.Span.SetTag("numAgents", agents.Count);
			scope.Span.SetTag("count", count);

			DescribeInstancesResponse describeResponse;
			using (IScope describeScope = GlobalTracer.Instance.BuildSpan("DescribeInstances").StartActive())
			{
				// Find stopped instances in the correct pool
				DescribeInstancesRequest describeRequest = new DescribeInstancesRequest();
				describeRequest.Filters = new List<Filter>();
				describeRequest.Filters.Add(new Filter("instance-state-name", new List<string> { InstanceStateName.Stopped.Value }));
				describeRequest.Filters.Add(new Filter("tag:" + PoolTagName, new List<string> { pool.Name }));
				describeResponse = await _client.DescribeInstancesAsync(describeRequest);
				describeScope.Span.SetTag("res.statusCode", (int)describeResponse.HttpStatusCode);
				describeScope.Span.SetTag("res.numReservations", describeResponse.Reservations.Count);
			}

			using (IScope startScope = GlobalTracer.Instance.BuildSpan("StartInstances").StartActive())
			{
				// Try to start the given instances
				StartInstancesRequest startRequest = new StartInstancesRequest();
				startRequest.InstanceIds.AddRange(describeResponse.Reservations.SelectMany(x => x.Instances).Select(x => x.InstanceId).Take(count));
				
				startScope.Span.SetTag("req.instanceIds", String.Join(",", startRequest.InstanceIds));
				if (startRequest.InstanceIds.Count > 0)
				{
					StartInstancesResponse startResponse = await _client.StartInstancesAsync(startRequest);
					startScope.Span.SetTag("res.statusCode", (int)startResponse.HttpStatusCode);
					startScope.Span.SetTag("res.numInstances", startResponse.StartingInstances.Count);
					if ((int)startResponse.HttpStatusCode >= 200 && (int)startResponse.HttpStatusCode <= 299)
					{
						foreach (InstanceStateChange instanceChange in startResponse.StartingInstances)
						{
							_logger.LogInformation("Starting instance {InstanceId} for pool {PoolId} (prev state {PrevState}, current state {CurrentState}", instanceChange.InstanceId, pool.Id, instanceChange.PreviousState, instanceChange.CurrentState);
						}
					}
				}

				if (startRequest.InstanceIds.Count < count)
				{
					_logger.LogInformation("Unable to expand pool {PoolName} with the requested number of instances. " +
					                      "Num requested instances to add {RequestedCount}. Actual instances started {InstancesStarted}", pool.Name, count, startRequest.InstanceIds.Count);
				}
			}
		}

		/// <inheritdoc/>
		public async Task ShrinkPoolAsync(IPool pool, IReadOnlyList<IAgent> agents, int count)
		{
			using IScope scope = GlobalTracer.Instance.BuildSpan("ShrinkPool").StartActive();
			scope.Span.SetTag("poolName", pool.Name);
			scope.Span.SetTag("count", count);
			
			string awsTagProperty = $"{AwsTagPropertyName}={PoolTagName}:{pool.Name}";
			
			// Sort the agents by number of active leases. It's better to shutdown agents currently doing nothing.
			List<IAgent> filteredAgents = agents.OrderBy(x => x.Leases.Count).ToList();
			List<IAgent> agentsWithAwsTags = filteredAgents.Where(x => x.HasProperty(awsTagProperty)).ToList(); 
			List<IAgent> agentsLimitedByCount = agentsWithAwsTags.Take(count).ToList();
			
			scope.Span.SetTag("agents.num", agents.Count);
			scope.Span.SetTag("agents.filtered.num", filteredAgents.Count);
			scope.Span.SetTag("agents.withAwsTags.num", agentsWithAwsTags.Count);
			scope.Span.SetTag("agents.limitedByCount.num", agentsLimitedByCount.Count);

			foreach (IAgent agent in agentsLimitedByCount)
			{
				IAuditLogChannel<AgentId> agentLogger = _agentCollection.GetLogger(agent.Id);
				if (await _agentCollection.TryUpdateSettingsAsync(agent, bRequestShutdown: true, shutdownReason: "Autoscaler") != null)
				{
					agentLogger.LogInformation("Marked {AgentId} in pool {PoolName} for shutdown due to autoscaling (currently {NumLeases} leases outstanding)", agent.Id, pool.Name, agent.Leases.Count);
				}
				else
				{
					agentLogger.LogError("Unable to mark agent {AgentId} in pool {PoolName} for shutdown due to autoscaling", agent.Id, pool.Name);
				}
			}
		}

		/// <inheritdoc/>
		public async Task<int> GetNumStoppedInstancesAsync(IPool pool)
		{
			// Find all instances in the pool
			DescribeInstancesRequest describeRequest = new DescribeInstancesRequest();
			describeRequest.Filters = new List<Filter>();
			describeRequest.Filters.Add(new Filter("instance-state-name", new List<string> { InstanceStateName.Stopped.Value }));
			describeRequest.Filters.Add(new Filter("tag:" + PoolTagName, new List<string> { pool.Name }));

			DescribeInstancesResponse describeResponse = await _client.DescribeInstancesAsync(describeRequest);
			return describeResponse.Reservations.SelectMany(x => x.Instances).Select(x => x.InstanceId).Distinct().Count();
		}
	}
}

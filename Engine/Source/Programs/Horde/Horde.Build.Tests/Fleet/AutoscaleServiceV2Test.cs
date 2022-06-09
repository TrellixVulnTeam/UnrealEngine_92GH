// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Horde.Build.Fleet.Autoscale;
using Horde.Build.Models;
using Horde.Build.Services;
using Horde.Build.Utilities;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using StatsdClient;

namespace Horde.Build.Tests.Fleet
{
	using PoolId = StringId<IPool>;

	public class FleetManagerSpy : IFleetManager
	{
		public int ExpandPoolAsyncCallCount { get; private set; }
		public int ShrinkPoolAsyncCallCount { get; private set; }
		
		public Task ExpandPoolAsync(IPool pool, IReadOnlyList<IAgent> agents, int count)
		{
			ExpandPoolAsyncCallCount++;
			return Task.CompletedTask;
		}

		public Task ShrinkPoolAsync(IPool pool, IReadOnlyList<IAgent> agents, int count)
		{
			ShrinkPoolAsyncCallCount++;
			return Task.CompletedTask;
		}

		public Task<int> GetNumStoppedInstancesAsync(IPool pool)
		{
			throw new NotImplementedException();
		}
	}

	public class PoolSizeStrategySpy : IPoolSizeStrategy
	{
		public int CallCount { get; private set; }
		public HashSet<PoolId> PoolIdsSeen { get; } = new();
		
		public Task<List<PoolSizeData>> CalcDesiredPoolSizesAsync(List<PoolSizeData> pools)
		{
			CallCount++;
			foreach (PoolSizeData data in pools)
			{
				PoolIdsSeen.Add(data.Pool.Id);
			}
			return Task.FromResult(pools);
		}

		public string Name { get; } = "PoolSizeStrategySpy";
	}

	[TestClass]
	public class AutoscaleServiceV2Test : TestSetup
	{
		readonly FleetManagerSpy _fleetManagerSpy = new();
		readonly IDogStatsd _dogStatsD = new NoOpDogStatsd();

		[TestMethod]
		public async Task PerPoolStrategy()
		{
			using AutoscaleServiceV2 service = GetAutoscaleService(_fleetManagerSpy);
			
			IPool pool1 = await PoolService.CreatePoolAsync("bogusPoolLease1", null, true, 0, 0, sizeStrategy: PoolSizeStrategy.LeaseUtilization);
			IPool pool2 = await PoolService.CreatePoolAsync("bogusPoolLease2", null, true, 0, 0, sizeStrategy: null);
			IPool pool3 = await PoolService.CreatePoolAsync("bogusPoolJobQueue1", null, true, 0, 0, sizeStrategy: PoolSizeStrategy.JobQueue);
			IPool pool4 = await PoolService.CreatePoolAsync("bogusPoolJobQueue2", null, true, 0, 0, sizeStrategy: PoolSizeStrategy.JobQueue);
			IPool pool5 = await PoolService.CreatePoolAsync("bogusPoolNoOp", null, true, 0, 0, sizeStrategy: PoolSizeStrategy.NoOp);

			PoolSizeStrategySpy leaseUtilizationSpy = new();
			PoolSizeStrategySpy jobQueueSpy = new();
			PoolSizeStrategySpy noOpSpy = new();
			service.OverridePoolSizeStrategiesDuringTesting(leaseUtilizationSpy, jobQueueSpy, noOpSpy);

			await service.TickLeaderAsync(CancellationToken.None);
			
			Assert.AreEqual(1, leaseUtilizationSpy.CallCount);
			Assert.IsTrue(leaseUtilizationSpy.PoolIdsSeen.Contains(pool1.Id));
			Assert.IsTrue(leaseUtilizationSpy.PoolIdsSeen.Contains(pool2.Id));
			
			Assert.AreEqual(1, jobQueueSpy.CallCount);
			Assert.IsTrue(jobQueueSpy.PoolIdsSeen.Contains(pool3.Id));
			Assert.IsTrue(jobQueueSpy.PoolIdsSeen.Contains(pool4.Id));
			
			Assert.AreEqual(1, noOpSpy.CallCount);
			Assert.IsTrue(noOpSpy.PoolIdsSeen.Contains(pool5.Id));
		}

		[TestMethod]
		public async Task ScaleOutCooldown()
		{
			using AutoscaleServiceV2 service = GetAutoscaleService(_fleetManagerSpy);
			IPool pool = await PoolService.CreatePoolAsync("testPool", null, true, 0, 0, sizeStrategy: PoolSizeStrategy.NoOp);

			// First scale-out will succeed
			await service.ResizePools(new() { new PoolSizeData(pool, new List<IAgent>(), 1, "Testing") });
			Assert.AreEqual(1, _fleetManagerSpy.ExpandPoolAsyncCallCount);
			
			// Cannot scale-out due to cool-down
			await service.ResizePools(new() { new PoolSizeData(pool, new List<IAgent>(), 2, "Testing") });
			Assert.AreEqual(1, _fleetManagerSpy.ExpandPoolAsyncCallCount);

			// Wait some time and then try again
			await Clock.AdvanceAsync(TimeSpan.FromHours(2));
			await service.ResizePools(new() { new PoolSizeData(pool, new List<IAgent>(), 2, "Testing") });
			Assert.AreEqual(2, _fleetManagerSpy.ExpandPoolAsyncCallCount);
		}
		
		[TestMethod]
		public async Task ScaleInCooldown()
		{
			using AutoscaleServiceV2 service = GetAutoscaleService(_fleetManagerSpy);
			IPool pool = await PoolService.CreatePoolAsync("testPool", null, true, 0, 0, sizeStrategy: PoolSizeStrategy.NoOp);
			IAgent agent1 = await CreateAgentAsync(pool);
			IAgent agent2 = await CreateAgentAsync(pool);

			// First scale-out will succeed
			await service.ResizePools(new() { new PoolSizeData(pool, new () { agent1, agent2 }, 1, "Testing") });
			Assert.AreEqual(1, _fleetManagerSpy.ShrinkPoolAsyncCallCount);
			
			// Cannot scale-out due to cool-down
			await service.ResizePools(new() { new PoolSizeData(pool, new () { agent1 }, 0, "Testing") });
			Assert.AreEqual(1, _fleetManagerSpy.ShrinkPoolAsyncCallCount);

			// Wait some time and then try again
			await Clock.AdvanceAsync(TimeSpan.FromHours(2));
			await service.ResizePools(new() { new PoolSizeData(pool, new () { agent1 }, 0, "Testing") });
			Assert.AreEqual(2, _fleetManagerSpy.ShrinkPoolAsyncCallCount);
		}

		private AutoscaleServiceV2 GetAutoscaleService(IFleetManager fleetManager)
		{
			ILogger<AutoscaleServiceV2> logger = ServiceProvider.GetRequiredService<ILogger<AutoscaleServiceV2>>();
			IOptions<ServerSettings> serverSettingsOpt = ServiceProvider.GetRequiredService<IOptions<ServerSettings>>();

			LeaseUtilizationStrategy leaseUtilizationStrategy = new(AgentCollection, PoolCollection, LeaseCollection, Clock);
			JobQueueStrategy jobQueueStrategy = new(JobCollection, GraphCollection, StreamService, Clock);
			AutoscaleServiceV2 service = new AutoscaleServiceV2(leaseUtilizationStrategy,jobQueueStrategy, new NoOpPoolSizeStrategy(), AgentCollection, PoolCollection, fleetManager, _dogStatsD, Clock, serverSettingsOpt, logger);
			return service;
		}
	}
}
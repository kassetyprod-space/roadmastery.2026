// Простой плоский стенд для headless-тестов ядра: кузов с 3 степенями свободы (x, y, рыскание),
// квазистатический перенос нагрузки вместо полноценной вертикальной динамики.
// В игре эту роль играет Chaos (Unreal), здесь — чтобы гонять физику в CI без движка.
#pragma once

#include "vsim/Vehicle.h"

#include <cmath>
#include <vector>

struct PlanarRig
{
	vsim::Vehicle& V;
	double Mass;
	double Izz;
	double CgH;

	double PosX = 0.0;
	double PosY = 0.0;
	double Yaw = 0.0;
	double U = 0.0;   // продольная скорость кузова
	double Vb = 0.0;  // поперечная (вправо)
	double R = 0.0;   // скорость рыскания
	double Ax = 0.0;
	double Ay = 0.0;
	// Сглаженные ускорения для переноса нагрузки: имитируют инерцию крена и галопирования,
	// без них квазистатический перенос даёт положительную обратную связь и разносит стенд.
	double AxLoad = 0.0;
	double AyLoad = 0.0;
	double SlopeRad = 0.0; // > 0 — подъём по ходу движения
	vsim::SurfaceInfo Surface;
	std::vector<vsim::WheelContact> Contacts;

	explicit PlanarRig(vsim::Vehicle& InV)
		: V(InV)
		, Mass(InV.Config().Mass)
		, Izz(InV.Config().YawInertia)
		, CgH(InV.Config().CgHeight)
	{
		Contacts.resize(V.NumWheels());
	}

	void Step(double Dt)
	{
		const auto& Cfg = V.Config();
		double XFront = -1e9, XRear = 1e9, TrackSum = 0.0;
		for (const auto& W : Cfg.Wheels)
		{
			XFront = std::max(XFront, W.X);
			XRear = std::min(XRear, W.X);
			TrackSum += std::abs(W.Y);
		}
		const double Base = XFront - XRear;
		const double HalfTrack = TrackSum / Cfg.Wheels.size();
		const double Weight = Mass * vsim::kGravity * std::cos(SlopeRad);
		const double FrontShare = -XRear / Base;
		const double Tau = 0.15;
		AxLoad += (Ax - AxLoad) * std::min(1.0, Dt / Tau);
		AyLoad += (Ay - AyLoad) * std::min(1.0, Dt / Tau);
		const double LongTransfer = Mass * AxLoad * CgH / Base;
		const double LatTransfer = Mass * AyLoad * CgH / (2.0 * HalfTrack);

		// Желаемая нагрузка на колесо (сумма по оси сохраняется, внутреннее не уходит в минус).
		std::vector<double> Fz(V.NumWheels(), 0.0);
		for (size_t I = 0; I < V.NumWheels(); ++I)
		{
			const auto& W = Cfg.Wheels[I];
			const bool bFront = W.X > 0.0;
			const double Share = bFront ? FrontShare : 1.0 - FrontShare;
			const double AxleLoad = std::max(0.0, Weight * Share + (bFront ? -1.0 : 1.0) * LongTransfer);
			const double Lat = vsim::Clamp(LatTransfer * Share, -AxleLoad, AxleLoad);
			Fz[I] = 0.5 * AxleLoad + (W.Side < 0 ? 0.5 : -0.5) * Lat;
		}

		for (size_t I = 0; I < V.NumWheels(); ++I)
		{
			const auto& W = Cfg.Wheels[I];
			// Обратная задача подвески с учётом стабилизатора: найти ходы, дающие нужные нагрузки.
			const double Arb = W.Axle < static_cast<int>(Cfg.AntiRollRate.size()) ? Cfg.AntiRollRate[W.Axle] : 0.0;
			double Other = Fz[I];
			for (size_t J = 0; J < V.NumWheels(); ++J)
			{
				if (J != I && Cfg.Wheels[J].Axle == W.Axle)
				{
					Other = Fz[J];
				}
			}
			const double K = W.Suspension.SpringRate;
			const double Sum = (Fz[I] + Other - 2.0 * W.Suspension.Preload) / K;  // xI + xOther
			const double Diff = (Fz[I] - Other) / (K + 2.0 * Arb);                 // xI - xOther

			vsim::WheelContact& C = Contacts[I];
			C.bInContact = Fz[I] > 1.0;
			C.Compression = std::max(0.0, 0.5 * (Sum + Diff));
			C.CompressionVelocity = 0.0;
			C.Surface = Surface;

			const double Delta = V.SteerAngle(I);
			const double Vx = U - R * W.Y;
			const double Vy = Vb + R * W.X;
			C.Vx = std::cos(Delta) * Vx + std::sin(Delta) * Vy;
			C.Vy = -std::sin(Delta) * Vx + std::cos(Delta) * Vy;
		}

		V.Step(Dt, Contacts, U);

		double Fx = 0.0, Fy = 0.0, Mz = 0.0;
		for (size_t I = 0; I < V.NumWheels(); ++I)
		{
			const auto& W = Cfg.Wheels[I];
			const auto& O = V.GetWheel(I);
			const double Delta = O.SteerAngle;
			const double Fbx = std::cos(Delta) * O.Fx - std::sin(Delta) * O.Fy;
			const double Fby = std::sin(Delta) * O.Fx + std::cos(Delta) * O.Fy;
			Fx += Fbx;
			Fy += Fby;
			Mz += W.X * Fby - W.Y * Fbx;
		}
		Fx -= vsim::SignOf(U) * V.AeroDragForce(U);
		Fx -= Mass * vsim::kGravity * std::sin(SlopeRad);

		Ax = Fx / Mass;
		Ay = Fy / Mass;
		U += (Ax + R * Vb) * Dt;
		Vb += (Ay - R * U) * Dt;
		R += Mz / Izz * Dt;
		Yaw += R * Dt;
		PosX += (std::cos(Yaw) * U - std::sin(Yaw) * Vb) * Dt;
		PosY += (std::sin(Yaw) * U + std::cos(Yaw) * Vb) * Dt;
	}

	double SpeedKmh() const { return U * 3.6; }
};

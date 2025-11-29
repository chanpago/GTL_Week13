#include "pch.h"
#include "BodySetup.h"

IMPLEMENT_CLASS(UBodySetup)

void FKAggregateGeom::Clear()
{
    SphereElements.Empty();
    BoxElements.Empty();
    SphylElements.Empty();
    ConvexElements.Empty();
}

void UBodySetup::AddSphere(const FKSphereElem& Elem)
{
    AggGeom.SphereElements.Add(Elem);
}

void UBodySetup::AddBox(const FKBoxElem& Elem)
{
    AggGeom.BoxElements.Add(Elem);
}

void UBodySetup::AddSphyl(const FKSphylElem& Elem)
{
    AggGeom.SphylElements.Add(Elem);
}
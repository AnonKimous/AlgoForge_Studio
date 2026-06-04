# Agent

This module owns the lightweight `Agent` data object.

## What It Keeps

- Agent name and algorithm name.
- The current algorithm package handles attached to this agent.
- An optional intervention package.
- The resource descriptor and compliance descriptor attached to this agent.
- The optional custom intervention UI hook owned by the intervention package.

## What It Does Not Do

- It does not own ticking.
- It does not own render/physics variants.
- It does not model a pipeline graph or route metadata.
- It does not carry extra mounted-agent or resource-binding layers.

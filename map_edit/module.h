#ifndef EQEMU_MAP_VIEW_MODULE_H
#define EQEMU_MAP_VIEW_MODULE_H

class Scene;
class Module
{
public: 
	Module() { m_running = false; m_unpaused = true; }
	virtual ~Module() { }
	
	virtual const char *GetName() = 0;
	virtual void OnLoad(Scene *s) = 0;
	virtual void OnShutdown() = 0;
	virtual void OnDrawMenu() = 0;
	virtual void OnDrawUI() = 0;
	virtual void OnSceneLoad(const char *zone_name) = 0;
	virtual void OnSuspend() = 0;
	virtual void OnResume() = 0;
	virtual bool HasWork() = 0;

	void SetRunning(bool v) { m_running = v; }
	bool& GetRunning() { return m_running; }

	void SetUnpaused(bool v) { m_unpaused = v; }
	bool& GetUnpaused() { return m_unpaused; }
protected:
	bool m_running;
	bool m_unpaused;
};

#endif
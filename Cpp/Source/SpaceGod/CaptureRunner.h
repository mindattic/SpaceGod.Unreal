#pragma once

// CaptureRunner: the C++ port of Unity.SpaceGod's Runtime/CaptureRunner.cs (US-T31).
// The MonoBehaviour is a plain class owned by whatever carries the app (Bootstrap adds it on -demo): the host calls
// Start() once, before the first frame's UnitySpaceApp.Update, then Update() every frame after the app's Update -- the
// point in Unity's frame where coroutines waiting on `yield return null` resume. Its coroutines (the C# Start is one,
// and it starts the tour as another) run on SceneTour::Runner; see SceneTour.h for how they are stepped.

#include "CoreMinimal.h"
#include "Cs/Cs.h"
#include "SceneTour.h"

namespace SpaceGod
{
	class UnitySpaceApp;

	/// US-T695: the run's current MindAttic.Media.Capture preview set, shared by the tour (-demo -capture -preview) and the
	/// sg.Shot console command (Console/ShotCommand.cpp), so every shot of the run lands in one set.
	namespace PreviewSet
	{
		/// The current set's folder, or empty when none was made yet.
		FString Current();

		/// Current(), first making it (`capture set new`, titled `title`, tagged `ticket`) when there is none. Empty when
		/// the capture agent is missing or failed.
		FString Ensure(const FString& title, const FString& ticket = FString());

		/// "<prefix> yyyy-mm-dd": a set title with today's date.
		FString DatedTitle(const TCHAR* prefix);

		/// `capture set add`, once `file` exists (an Unreal screenshot is written at the end of its frame): polled on the
		/// core ticker for up to 10 s, then the file is copied into `dir` as one item captioned `caption`.
		void AddWhenWritten(const FString& dir, const FString& file, const FString& tag, const FString& caption);
	}

	/// Unity's ScreenCapture.CaptureScreenshot (here standing in for MindAttic.Media.Capture's ScreenshotAsync), implemented
	/// on Unreal's screenshot request (CaptureBackend.cpp). As Unity's, the file is written at the end of the frame.
	namespace CaptureBackend
	{
		void Screenshot(const cs::String& path);
	}

	/// Started by <c>-demo</c> (see Bootstrap): runs the SceneTour
	/// once, then quits. Used to verify a build visually and to produce the README images. Add
	/// <c>-capture screen</c> (or <c>video</c>, or both) to save a screenshot at each stop through
	/// MindAttic.Media.Capture — shots go to R:\MindAttic\Capture\Unity.SpaceGod\Unity.SpaceGod_{timestamp}.png,
	/// each sidecar tagged with its stop. Without <c>-capture</c> the tour still plays, just with no screenshots.
	///
	/// Unreal (US-T72): with <c>-capture</c> each stop is <c>ScreenshotAsync(&lt;stop&gt;)</c> through MindAttic.Media.Capture's
	/// host (the MindAttic.Media.Capture.Unreal plugin), into the session folder under R:\MindAttic\Capture\SpaceGod\, as in
	/// Unity. With tools/play.ps1's <c>-capturedir=&lt;dir&gt;</c> the stops stay Unreal screenshots through CaptureBackend,
	/// saved as <c>&lt;dir&gt;/&lt;stop&gt;.png</c>, with no capture host.
	///
	/// US-T695: <c>-preview</c> (or <c>-preview=&lt;ticket&gt;</c>) beside <c>-capture</c> also makes a preview set at tour
	/// start ("SpaceGod tour" and the date), puts each stop's shot into it captioned with the stop's name, and opens it in
	/// MindAttic.Media.Viewer when the tour quits. Without it the tour is as before.
	class CaptureRunner final
	{
	public:
		/// `GetComponent<UnitySpaceApp>()`: the app on the same GameObject (it outlives this runner).
		explicit CaptureRunner(UnitySpaceApp* app);
		~CaptureRunner();
		CaptureRunner(const CaptureRunner&) = delete;
		CaptureRunner& operator=(const CaptureRunner&) = delete;

		/// `IEnumerator Start()`: starts the Start coroutine, running its first step now.
		void Start();

		/// C++ only: Unity's coroutine scheduler for this component, once a frame after the app's Update: the tour
		/// (started first) resumes, then the Start coroutine.
		void Update();

		/// Object.Destroy of the component: Unity stops its coroutines with it.
		void OnDestroy();

		/// C++ only: `app.GetComponent<CaptureRunner>()` -- the live runner, or null once there is none.
		static CaptureRunner* Current();

	private:
		UnitySpaceApp* hostApp;
		bool started = false;
		/// The Start coroutine, and the tour it starts (StartCoroutine(RunTour(...))).
		SceneTour::Runner startCoroutine, tour;

		SceneTour::Routine StartRoutine();

		static SceneTour::Routine RunTour(UnitySpaceApp& app, bool capturing, cs::Action<> finished);
		static SceneTour::Routine NoShot(cs::String name);
		static SceneTour::Routine Shot(cs::String name);

		static CaptureRunner*& CurrentSlot();

		/// US-T695: -preview's set for this tour (empty without -preview, or when the agent could not make one).
		static FString& TourSet();
	};
}

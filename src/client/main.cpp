#include "std_include.hpp"
#include "window.hpp"
#include "rocktree.hpp"

int main(int argc, char** argv)
{
	rocktree rocktree{"earth"};

	window window(800, 600, "game");

	window.show();

	return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
	return main(__argc, __argv);
}

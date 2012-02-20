#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <SDL.h>
#include <magick/MagickCore.h>

#define APP_NAME "Frame"
#define Q(x) ((unsigned char)((double)(x) / (double)MaxRGB * 255.0) & 0xff)

typedef struct FileEntry {
	char *path;
	struct FileEntry *prev;
	struct FileEntry *next;
} FileEntry;

typedef enum {
	false = 0,
	true = 1
} bool;

FileEntry *head = 0;
bool randomOrder = true;
int fadeStep = 8;
int timeToShow = 10; // seconds
time_t lastImage = 0;
SDL_Texture *texture = 0;
SDL_Texture *newTexture = 0;
bool texturePending = false;
SDL_Surface *canvas = 0;
SDL_Thread *thread = 0;
bool run = true;
SDL_mutex *lock = 0;
int width = 1024;
int height = 768;

bool IsVM() {
	bool vm = false;
	FILE *f = 0;
	if((f = fopen("/proc/cpuinfo", "r"))) {
		char buf[255];
		while(fgets(buf, sizeof(buf) / sizeof(char), f)) {
			if(strstr(buf, "hypervisor")) {
				vm = true;
				break;
			}
		}
		fclose(f);
	}
	return vm;
}

SDL_Renderer *Initialize(int width, int height) {
	SDL_Renderer *screen = 0;

	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
		return 0;
	}

	SDL_SetHint("SDL_HINT_FRAMEBUFFER_ACCELERATION", "1");
	SDL_SetHint("SDL_HINT_RENDER_DRIVER", "opengl");
	if(IsVM()) {
		printf("Hypervisor detected: forcing software rendering\n");
		SDL_putenv("SDL_RENDER_DRIVER=software");
	}
	SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", "linear");
	SDL_SetHint("SDL_HINT_RENDER_VSYNC", "1");

	SDL_Window *window = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);

	if(!(screen = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC))) {
		fprintf(stderr, "Unable to set resolution: %s\n", SDL_GetError());
		SDL_Quit();
		return 0;
	}
	SDL_RendererInfo ri;
	SDL_GetRendererInfo(screen, &ri);

	printf("Using renderer: %s\n", ri.name);
	//printf("Renderer flags: %d\n", ri.flags);

	SDL_ShowCursor(SDL_DISABLE);

	return screen;
}

SDL_Surface *LoadImage(char *input, unsigned long width, unsigned long height) {
	SDL_Surface *result = 0;
	ExceptionInfo *exception = AcquireExceptionInfo();
	ImageInfo *info = AcquireImageInfo();
	strcpy(info->filename, input);
	Image *image = ReadImage(info, exception);
	MagickBooleanType enlarging = MagickFalse;
	if(width > image->columns || height > image->rows)
		enlarging = MagickTrue;
	//Image *scaledImage = ResizeImage(image, width, height, enlarging ? MitchellFilter : LanczosFilter, 1.0, exception);
	//if(scaledImage == 0)
	//	MagickError(exception->severity, exception->reason, exception->description);
	//else {
	Image *scaledImage = image;
	char geom[128];
	snprintf(geom, sizeof(geom), "%lux%lu", width, height);
	//printf("Scaling to %s\n", geom);
	TransformImage(&scaledImage, 0, geom);
	{
		result = SDL_CreateRGBSurface(0, scaledImage->columns, scaledImage->rows, 32, 0, 0, 0, 0);
		if(result) {
			unsigned char *pixels = (unsigned char*)result->pixels;
			SDL_LockSurface(result);
			const PixelPacket *imagePixels = GetVirtualPixels(scaledImage, 0, 0, scaledImage->columns, scaledImage->rows, exception);
			for(int y = 0; y < scaledImage->rows; y++) {
				for(int x = 0; x < scaledImage->columns; x++) {
					// offset to the correct row (y * result->pitch) and then to the correct column (bytes/pixel * x)
					int offset = y * result->pitch + result->format->BytesPerPixel * x;
					int srcOffset = y * scaledImage->columns + x;
					pixels[offset + 3] = Q(imagePixels[srcOffset].red);
					pixels[offset + 2] = Q(imagePixels[srcOffset].green);
					pixels[offset + 1] = Q(imagePixels[srcOffset].blue);
					pixels[offset + 0] = 0xff;
				}
			}
			SDL_UnlockSurface(result);
		}
		//DestroyImage(scaledImage);
	}
	DestroyImage(scaledImage);
	DestroyImageInfo(info);
	exception = DestroyExceptionInfo(exception);
	return result;
}

unsigned int FileCount(FileEntry *head) {
	unsigned int result = 0;
	FileEntry *ptr = head;
	while(ptr) {
		result++;
		ptr = ptr->next;
	}
	return result;
}

void RemoveFile(FileEntry *item) {
	if(head == 0 || item == 0)
		return;
	if(item == head) {
		free(item->path);
		head = item->next;
		free(item);
		if(head)
			head->prev = 0;
	} else {
		free(item->path);
		if(item->prev)
			item->prev->next = item->next;
		free(item);
	}
}

FileEntry *GetNextFile() {
	FileEntry *ptr = head;
	if(randomOrder && head) {
		int c = rand() % FileCount(head);
		while(c > 0 && ptr) {
			ptr = ptr->next;
			c--;
		}
	}
	return ptr;
}

void LoadFileList() {
	FILE *f = popen("/usr/bin/ruby aperture.rb", "r");
	if(f) {
		char buf[255];
		FileEntry *last = 0;
		while(fgets(buf, sizeof(buf), f)) {
			while(buf[strlen(buf) - 1] == '\n' || buf[strlen(buf) - 1] == '\r')
				buf[strlen(buf) - 1] = 0;
			FileEntry *e = (FileEntry*)malloc(sizeof(FileEntry));
			e->path = strdup(buf);
			e->prev = last;
			e->next = 0;
			if(last)
				last->next = e;
			else
				head = e;
			last = e;
		}
		fclose(f);
	}
	unsigned int fileCount = FileCount(head);
	if(fileCount == 0) {
		fprintf(stderr, "No files to display, exiting\n");
		exit(0);
	}
	printf("Read %d files\n", fileCount);
}

int UpdateThread(void *unused) {
	while(run) {
		if(head == 0)
			LoadFileList();

		FileEntry *item = GetNextFile();
		if(item == 0)
			break;
		SDL_Surface *image = LoadImage(item->path, width, height);
		RemoveFile(item);
		lastImage = time(0);

		SDL_LockMutex(lock);
		canvas = SDL_CreateRGBSurface(0, width, height, 32, 0, 0, 0, 0);
		SDL_Rect centered = { (width - image->w) / 2 , (height - image->h) / 2, image->w, image->h };
		SDL_BlitSurface(image, 0, canvas, &centered);
		if(SDL_MUSTLOCK(canvas))
			SDL_LockSurface(canvas);
		SDL_UpdateTexture(newTexture, 0, canvas->pixels, width * 4);
		if(SDL_MUSTLOCK(canvas))
			SDL_UnlockSurface(canvas);
		SDL_UnlockMutex(lock);
		SDL_FreeSurface(image);
		texturePending = true;
		
		// wait for next image to get displayed
		while(run && texturePending) {
			SDL_Delay(500);
		}
		// wait for remainder of duration
		while(run && time(0) < lastImage + timeToShow) {
			SDL_Delay(500);
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	srand(getpid() * time(0));
	LoadFileList();

	Display *dpy = XOpenDisplay(0);
	if(dpy) {
		int s = DefaultScreen(dpy);

		width = DisplayWidth(dpy, s);
		height = DisplayHeight(dpy, s);

		XCloseDisplay(dpy);
	}
		
	SDL_Event event;
	SDL_Renderer *screen = Initialize(width, height);
	if(screen == 0)
			return -1;
			
	MagickCoreGenesis(APP_NAME, MagickTrue);
			
	SDL_Rect position = { 0, 0, width, height };
	texture = SDL_CreateTexture(screen, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	newTexture = SDL_CreateTexture(screen, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
	SDL_SetTextureBlendMode(newTexture, SDL_BLENDMODE_ADD);
	lock = SDL_CreateMutex();
	thread = SDL_CreateThread(UpdateThread, 0, 0);
	int alpha = 0;

	while(run) {
		SDL_RenderClear(screen);

		SDL_LockMutex(lock);
		SDL_SetTextureAlphaMod(texture, 255 - alpha);
		SDL_SetTextureAlphaMod(newTexture, alpha);

		SDL_RenderCopy(screen, texture, 0, &position);

		if(texturePending) {
			alpha += 8;
			if(alpha >= 255) {
				if(canvas) {
					if(SDL_MUSTLOCK(canvas))
						SDL_LockSurface(canvas);
					SDL_UpdateTexture(texture, 0, canvas->pixels, width * 4);
					if(SDL_MUSTLOCK(canvas))
						SDL_UnlockSurface(canvas);
					SDL_FreeSurface(canvas);
					canvas = 0;
				}
				texturePending = false;
				alpha = 0;
			}
		}
		SDL_RenderCopy(screen, newTexture, 0, &position);
		SDL_UnlockMutex(lock);

		SDL_RenderPresent(screen);
				
		while(SDL_PollEvent(&event)) {
			switch(event.type) {
				case SDL_QUIT:
					run = false;
					break;
				case SDL_KEYDOWN:
					if(event.key.keysym.sym == SDLK_ESCAPE)
						run = false;
					break;
				case SDL_WINDOWEVENT_CLOSE:
					run = false;
					break;
			}
			break;
		}
	}
	
	texturePending = false;
	SDL_WaitThread(thread, 0);

	SDL_DestroyRenderer(screen);
	SDL_DestroyMutex(lock);
	SDL_Quit();
	
	MagickCoreTerminus();

	return 0;
}

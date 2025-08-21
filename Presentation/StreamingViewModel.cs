using Services;

namespace Presentation
{
    public class StreamingViewModel
    {
        private IStreamingService _streamingService;

        public StreamingViewModel(IStreamingService streamingService)
        {
            _streamingService = streamingService;
        }
    }
}

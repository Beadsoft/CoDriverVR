using UnityEngine;

public sealed class PassengerVideoSurface : MonoBehaviour
{
    [SerializeField] private Renderer targetRenderer;
    [SerializeField] private float distance = 2.0f;
    [SerializeField] private Vector2 size = new(2.2f, 1.25f);

    private void Awake()
    {
        if (targetRenderer == null)
        {
            targetRenderer = GetComponent<Renderer>();
        }

        transform.localScale = new Vector3(size.x, size.y, 1f);
    }

    private void LateUpdate()
    {
        var cameraTransform = Camera.main != null ? Camera.main.transform : null;
        if (cameraTransform == null)
        {
            return;
        }

        transform.position = cameraTransform.position + cameraTransform.forward * distance;
        transform.rotation = Quaternion.LookRotation(transform.position - cameraTransform.position, Vector3.up);
    }

    public void SetTexture(Texture texture)
    {
        if (targetRenderer != null && texture != null)
        {
            targetRenderer.material.mainTexture = texture;
        }
    }
}
